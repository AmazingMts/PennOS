#ifndef P_HANDLER_H
#define P_HANDLER_H

#include "struct.h"

// Define constants if not already defined, but avoid redefining standard ones
// if <signal.h> is included
#ifndef HOST_SIGINT
#define HOST_SIGINT 2  // Ctrl-C
#endif

#ifndef HOST_SIGTSTP
#define HOST_SIGTSTP 20  // Ctrl-Z
#endif

#ifndef HOST_SIGQUIT
#define HOST_SIGQUIT 3  // Ctrl-Backslash
#endif

/**
 * @brief Sets up host OS signal handlers for Ctrl-C, Ctrl-Backslash and Ctrl-Z.
 */
void setup_host_signals(void);

/**
 * @brief Check if any host signals were received and handle them safely.
 *        This should be called from the scheduler loop.
 */
void k_check_host_signals(void);

#endif  // P_HANDLER_H
