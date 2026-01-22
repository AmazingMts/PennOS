#include <stdio.h>
#include <stdlib.h>

#include "../scheduler.h"
#include "Vec.h"  // Vec implementation
#include "queue.h"
#include "struct.h"

static priority_queues_t prio_q;
static blocked_queue_t blocked_q;

// Initialize all queues
void k_queues_init() {
  // priority queue
  for (int i = 0; i < NUM_PRIO; i++) {
    prio_q[i] = vec_new(16, NULL);
  }
  // blocked queue
  blocked_q.blocked_queue = vec_new(16, NULL);
}

// Destroy all queues
void k_queues_destroy() {
  for (int i = 0; i < NUM_PRIO; i++) {
    vec_destroy(&prio_q[i]);
  }
  vec_destroy(&blocked_q.blocked_queue);
}

// --- Ready Queue Operations ---
bool is_pq_empty(int prio) {
  return vec_len(&prio_q[prio]) == 0;
}

bool is_bq_empty() {
  return vec_len(&blocked_q.blocked_queue) == 0;
}

void k_enqueue(pcb_t* proc) {
  if (!proc || proc->state != P_READY)
    return;

  int prio = proc->prio;
  if (prio < 0 || prio >= NUM_PRIO)
    return;

  vec_push_back(&prio_q[prio], proc);
}

pcb_t* k_dequeue(int prio) {
  if (prio < 0 || prio >= NUM_PRIO || vec_len(&prio_q[prio]) == 0)
    return NULL;

  pcb_t* p = vec_get(&prio_q[prio], 0);
  vec_erase(&prio_q[prio], 0);
  return p;
}

void k_block(pcb_t* proc) {
  if (!proc)
    return;

  proc->state = P_BLOCKED;
  vec_remove(&prio_q[proc->prio], proc);
  vec_push_back(&blocked_q.blocked_queue, proc);

  // Log the blocking event
  k_log_event("BLOCKED", proc);
}

void k_unblock(pcb_t* proc) {
  if (!proc)
    return;

  vec_remove(&blocked_q.blocked_queue, proc);
  proc->state = P_READY;
  k_enqueue(proc);

  // Log the unblocking event
  k_log_event("UNBLOCKED", proc);
}

void k_stop(pcb_t* proc) {
  if (!proc)
    return;

  proc->state = P_STOPPED;
  proc->stopped_reported = false;
  vec_remove(&prio_q[proc->prio], proc);

  // scheduler will sleep-check the blocked queue, so when a process is stopped
  // it should be removed from the blocked queue (since it's no longer
  // 'sleeping').
  vec_remove(&blocked_q.blocked_queue, proc);
  pcb_t* parent = proc->parent;
  if (parent && parent->state == P_BLOCKED && parent->wake_tick == 0) {
    k_unblock(parent);
  }

  // Log the stopped event
  k_log_event("STOPPED", proc);
}

void k_continue(pcb_t* proc) {
  if (proc && proc->state == P_STOPPED) {
    proc->state = P_READY;
    k_enqueue(proc);

    // Log the continued event
    k_log_event("CONTINUED", proc);
  }
}

void k_tick_sleep_check(uint64_t tick) {
  size_t i = 0;
  while (i < vec_len(&blocked_q.blocked_queue)) {
    pcb_t* proc = (pcb_t*)vec_get(&blocked_q.blocked_queue, i);

    if (proc->wake_tick > 0 && proc->wake_tick <= tick) {
      proc->wake_tick = 0;
      k_unblock(proc);
    } else {
      i++;
    }
  }
}

void k_set_priority(pcb_t* proc, int prio) {
  if (!proc || prio < 0 || prio > 2 || proc->prio == prio) {
    return;
  }

  int old_prio = proc->prio;
  proc->prio = prio;

  // Log the NICE event
  k_log_nice_event(proc, old_prio, prio);

  if (proc->state == P_READY) {
    vec_remove(&prio_q[old_prio], proc);
    k_enqueue(proc);
  }
}

void k_remove_from_queues(pcb_t* proc) {
  if (!proc) {
    return;
  }

  // Try to remove from ready queues
  for (int i = 0; i < NUM_PRIO; i++) {
    vec_remove(&prio_q[i], proc);
  }

  // Try to remove from blocked queue
  vec_remove(&blocked_q.blocked_queue, proc);
}