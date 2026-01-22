#include "p_signal.h"
#include <stdio.h>
#include "../process.h"
#include "queue.h"

void k_signal_deliver(pcb_t* proc, psignal_t signal) {
  if (!proc) {
    return;
  }

  switch (signal) {
    case P_SIGTERM:
      // Terminate the process
      if (proc->state != P_ZOMBIE) {
        proc->exit_status = P_EXIT_SIGNALED;
        k_terminate(proc);
      }
      break;

    case P_SIGSTOP:
      // Stop the process
      if (proc->state != P_ZOMBIE) {
        k_stop(proc);
      }
      break;

    case P_SIGCONT:
      // Continue a stopped process
      if (proc->state == P_STOPPED) {
        k_continue(proc);
      }
      break;

    case P_SIGCHLD:
      // Child state change notification (typically handled by waitpid)
      // No action needed here
      break;

    default:
      break;
  }
}
