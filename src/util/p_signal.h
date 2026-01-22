#ifndef P_SIGNAL_H
#define P_SIGNAL_H

#include "struct.h"

typedef enum { P_SIGTERM, P_SIGSTOP, P_SIGCONT, P_SIGCHLD } psignal_t;

/**
 * @brief Deliver a signal to a process.
 *
 * @param proc The process to deliver the signal to.
 * @param signal The signal to deliver.
 */
void k_signal_deliver(pcb_t* proc, psignal_t signal);

#endif