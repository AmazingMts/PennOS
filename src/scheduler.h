#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "./util/struct.h"

// Global scheduler state (defined in scheduler.c)
extern uint64_t tick;
extern pcb_t* current;

/**
 * @brief Initialize the PennOS scheduler and start the periodic timer.
 *
 * This function sets up all global scheduler state and installs the SIGALRM
 * handler used for time-sliced scheduling:
 *
 * - Initializes the global tick counter, current running process, the scheduler
 *   signal mask (scheduler_mask) so that SIGALRM is the only signal unblocked
 *   during sigsuspend().
 * - Calls k_queues_init() to initialize all scheduler-managed queues
 *   (ready queues, blocked queue).
 * - Install empty handler for SIGALRM and unblocks SIGALRM.
 * - Install timer to deliver SIGALRM every 100 ms, which defines the scheduling
 *   time slice.
 *
 * @note This function should be called once during kernel startup, before
 * entering the main scheduling loop k_scheduler_run().
 */
void k_scheduler_init(const char* log_fname);

/**
 * @brief Main scheduler loop for PennOS.
 *
 * This function implements the core time-sliced scheduling logic:
 *
 * - Dequeues the next runnable process from the ready queues using
 *   k_pick_queue() and k_dequeue().
 * - If no process is runnable, calls k_idle() to put the system into
 *   an idle state (e.g., waiting for the next SIGALRM or wake-up event).
 * - Run the chosen process for a time slice.
 * - After each tick, wake any processes whose sleep interval has expired.
 * - If the current process used up its time slice and is still in
 *   state P_RUNNING, it is marked P_READY and enqueued again.
 * - Increments the global tick counter after each iteration.
 *
 * @note This function is intended to run on the main kernel thread and
 * normally does not return; it drives the entire scheduling of
 * user processes until the OS decides to shut down.
 */
void k_scheduler_run();

/**
 * @brief Idles the cpu until a signal is delivered.
 */
void k_idle();

/**
 * @brief Cleans up scheduler-related stuff
 */
void k_scheduler_cleanup();

/**
 * @brief Log a scheduler or process-related event to a file.
 *
 * This function appends a single log entry describing an event associated
 * with a given process. Each entry includes:
 *
 * - The current global tick value.
 * - A short event string (e.g., "SCHEDULE", "BLOCKED", "UNBLOCKED").
 * - The process ID (PID) and parent PID (PPID).
 * - The process state and priority.
 *
 * The function is a no-op if either @p event or @p pcb is NULL.
 *
 * @param event A human-readable description of the event to log.
 * @param pcb   Pointer to the process control block associated with the event.
 */
void k_log_event(const char* event, pcb_t* pcb);

/**
 * @brief Log a NICE (priority change) event.
 *
 * @param pcb      Pointer to the process control block.
 * @param old_prio The old priority value.
 * @param new_prio The new priority value.
 */
void k_log_nice_event(pcb_t* pcb, int old_prio, int new_prio);

#endif