#define _GNU_SOURCE
#include "scheduler.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#include "./util/p_handler.h"
#include "./util/queue.h"
#include "./util/struct.h"
#include "fat_kernel.h"
#include "process.h"

uint64_t tick;  /** Global tick counter */
pcb_t* current; /** Current running process */
static sigset_t scheduler_mask;
static const char* LOG_FILENAME = "log/log.txt";  // Default log file name
static const int schedule[] =
    {  // Fixed schedule array corresponding to 9:6:4 ratio
        0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 1, 0, 2, 0, 1, 0, 2, 1};
static int idx = 0;  // The 'pointer' to the next prio in schedule.

/**
 * @brief Does nothing (as intended)
 */
static void alarm_handler(int signo) {}

/**
 * @brief Choose a runnable priority queue using weighted random selection.
 *
 * This function inspects the three ready queues (priority 0, 1, and 2)
 * and returns the priority of the queue to schedule from.
 *
 * If more than one queue is non-empty, it performs a weighted random
 * selection with weights 9:6:4 for priorities 0, 1, and 2 respectively,
 * corresponding to an effective scheduling ratio of 2.25 : 1.5 : 1.
 *
 * If exactly one queue is non-empty, that queue is always chosen.
 * If all queues are empty, 0 is returned (the caller is expected to
 * handle the "no runnable process" case, e.g., by idling).
 *
 * @pre The random number generator should be initialized (e.g. with srand()).
 * @return An integer priority in the range [0, 2] indicating which
 *         priority queue to use next.
 */
static int k_pick_queue();

//////////////////////////////////////////////////////////////////////////////
// =================== Public API Implementation =================== ////////
////////////////////////////////////////////////////////////////////////////

void k_scheduler_init(const char* log_fname) {
  if (log_fname != NULL) {
    LOG_FILENAME = log_fname;
  }

  // Truncate the log file to start fresh on each run
  FILE* fp = fopen(LOG_FILENAME, "w");
  if (fp != NULL) {
    fclose(fp);
  }

  // initialize global value
  tick = 0;
  current = NULL;
  sigfillset(&scheduler_mask);
  sigdelset(&scheduler_mask, SIGALRM);
  k_queues_init();

  // just to make sure that
  // sigalrm doesn't terminate the process
  struct sigaction act = (struct sigaction){
      .sa_handler = alarm_handler,
      .sa_mask = scheduler_mask,
      .sa_flags = SA_RESTART,
  };
  sigaction(SIGALRM, &act, NULL);

  // make sure SIGALRM is unblocked
  sigset_t alarm_set;
  sigemptyset(&alarm_set);
  sigaddset(&alarm_set, SIGALRM);
  pthread_sigmask(SIG_UNBLOCK, &alarm_set, NULL);

  // set up timer
  struct itimerval it;
  it.it_interval = (struct timeval){.tv_usec = 100000};
  it.it_value = it.it_interval;
  setitimer(ITIMER_REAL, &it, NULL);
}

void k_scheduler_run() {
  pcb_t* last_scheduled = NULL;  // Track last scheduled process
  (void)last_scheduled;

  while (1) {
    // Check for deferred host signals
    k_check_host_signals();

    // Check if shutdown has been requested
    if (k_is_shutdown_requested()) {
      k_write(STDERR_FILENO, "Scheduler: Shutdown requested, exiting...\n", 42);
      break;
    }

    // pick next runnable process
    pcb_t* next = k_dequeue(k_pick_queue());

    if (!next) {
      // no runnable process: can idle
      k_idle();
      k_tick_sleep_check(tick);
      tick++;
      continue;
    }

    current = next;
    current->state = P_RUNNING;

    // Only log if we're switching to a different process
    // if (current != last_scheduled) {
    k_log_event("SCHEDULE", current);
    //   last_scheduled = current;
    // }

    // continue the thread, hang, and suspend it again
    spthread_continue(current->process);
    sigsuspend(&scheduler_mask);
    spthread_suspend(current->process);

    // afterward cleanup:
    k_tick_sleep_check(tick);

    // if process normally used up its time slice, requeue it
    if (current->state == P_RUNNING) {
      current->state = P_READY;
      k_enqueue(current);
    }

    current = NULL;
    tick++;
  }

  // Have exited the main while loop. OS is shutting down.
}

void k_idle() {
  sigsuspend(&scheduler_mask);
}

void k_scheduler_cleanup() {
  k_queues_destroy();
}

void k_log_event(const char* event, pcb_t* pcb) {
  if (!event || !pcb) {
    return;
  }

  // Open log file in append mode
  FILE* log_file = fopen(LOG_FILENAME, "a");
  if (log_file == NULL) {
    return;
  }

  // Log the event with process information
  fprintf(log_file, "[%5lu] %-10s %-5d %-4d %s\n", tick, event, pcb->pid,
          pcb->prio, pcb->cmd_name);

  fclose(log_file);
}

void k_log_nice_event(pcb_t* pcb, int old_prio, int new_prio) {
  if (!pcb) {
    return;
  }

  // Open log file in append mode
  FILE* log_file = fopen(LOG_FILENAME, "a");
  if (log_file == NULL) {
    return;
  }

  // Log format: [ticks] NICE PID OLD_NICE_VALUE NEW_NICE_VALUE PROCESS_NAME
  fprintf(log_file, "[%5lu] %-10s %-3d %-3d %-2d %s\n", tick, "NICE", pcb->pid,
          old_prio, new_prio, pcb->cmd_name);

  fclose(log_file);
}

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Implementations =================== //
////////////////////////////////////////////////////////////////////////////

static int k_pick_queue() {
  // check if any queue is empty
  bool has0 = !is_pq_empty(0);
  bool has1 = !is_pq_empty(1);
  bool has2 = !is_pq_empty(2);

  // When all queues are empty, just return an arbitrary prio
  if (!has0 && !has1 && !has2) {
    return 0;
  }

  const int size = sizeof(schedule) / sizeof(schedule[0]);
  // Iterate through the schedule to find the next available queue
  // We loop up to 'size' times to ensure we check everyone if needed
  for (int i = 0; i < size; i++) {
    int q = schedule[idx];

    // Always advance the index to maintain the "time slot" logic
    idx = (idx + 1) % size;

    // If the scheduled queue has processes, pick it
    if (q == 0 && has0)
      return 0;
    if (q == 1 && has1)
      return 1;
    if (q == 2 && has2)
      return 2;
  }

  // Fallback: This part should logically not be reached.
  return 0;
}