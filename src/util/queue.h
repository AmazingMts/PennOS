#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include "struct.h"

/**
 * @brief Initializes priority queues and block queue.
 */
void k_queues_init();

/**
 * @brief Destroies all queues.
 */
void k_queues_destroy();

/**
 * @brief Inspect if a specific priority queue is empty
 *
 * @param prio the intended priority queue
 * @return true if the queue is empty, false otherwise
 */
bool is_pq_empty(int prio);

/**
 * @brief Inspect if the blocked queue is empty
 *
 * @return true if the queue is empty, false otherwise
 */
bool is_bq_empty();

/**
 * @brief Enqueue a READY process into its priority queue.
 *
 * The function is a no-op if:
 * - proc is NULL,
 * - proc->state is not P_READY, or
 * - proc->prio is outside the valid range [0, NUM_PRIO).
 *
 * @param proc Pointer to the process to enqueue.
 */
void k_enqueue(pcb_t* proc);

/**
 * @brief Dequeue the next process from a given priority queue.
 *
 * If the priority is out of range, or the corresponding queue is empty,
 * the function returns NULL.
 *
 * @param prio The priority level of the queue to dequeue from
 *             (expected to be in the range [0, NUM_PRIO - 1]).
 * @return The dequeued pcb_t pointer on success, or NULL if the queue is
 *         empty or the priority is invalid.
 */
pcb_t* k_dequeue(int prio);

/**
 * @brief Block a process and move it from the ready queue to the blocked queue.
 *
 * It is a no-op if @p proc is NULL.
 *
 * @param proc Pointer to the process control block to block.
 */
void k_block(pcb_t* proc);

/**
 * @brief Unblock a process and move it back to the ready queue.
 *
 * It is a no-op if @p proc is NULL.
 *
 * @param proc Pointer to the process control block to unblock.
 */
void k_unblock(pcb_t* proc);

/**
 * @brief Stop a process without placing it in any ready or blocked queue.
 *
 * The process will not be scheduled again until k_continue() is called on it.
 * It is a no-op if @p proc is NULL.
 *
 * @param proc Pointer to the process control block to stop.
 */
void k_stop(pcb_t* proc);

/**
 * @brief Continue a previously stopped process by making it READY again.
 *
 * It is a no-op if @p proc is NULL or is not previously stopped.
 *
 * @param proc Pointer to the process control block to continue.
 */
void k_continue(pcb_t* proc);

/**
 * @brief Checks the blocked queue and wakes any processes whose sleep timer has
 * expired.
 */
void k_tick_sleep_check(uint64_t tick);

/**
 * @brief Update the priority level of a process. If the process was in ready
 * queue, move it accordingly.
 *
 * @param proc The target PCB.
 * @param prio The new priority level (0, 1, or 2).
 */
void k_set_priority(pcb_t* proc, int prio);

/**
 * @brief Remove a process from all queues (ready and blocked).
 *
 * This is used when a process becomes a zombie and should no longer
 * be in any scheduling queue.
 *
 * @param proc Pointer to the process control block to remove.
 */
void k_remove_from_queues(pcb_t* proc);

#endif