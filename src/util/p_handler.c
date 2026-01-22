#define _GNU_SOURCE
#include "p_handler.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "../process.h"
#include "../syscall.h"
#include "p_signal.h"
#include "struct.h"

// Signal flag to defer processing
volatile int pending_host_signal = 0;

/**
 * @brief Host OS signal handler for SIGINT (Ctrl-C), SIGQUIT (Ctrl-Backslash)
 * and SIGTSTP (Ctrl-Z).
 *
 * It determines the current PennOS foreground process (T-P-GID) and relays
 * the appropriate PennOS signal (P_SIGTERM or P_SIGSTOP) to it using s_kill().
 *
 * @param signum The host signal number (HOST_SIGINT, HOST_SIGQUIT or
 * HOST_SIGTSTP).
 */
void host_sig_handler(int signum) {
  // Only set the flag, do not perform complex operations here
  pending_host_signal = signum;
}

void k_check_host_signals(void) {
  if (pending_host_signal == 0) {
    return;
  }

  // Atomically read and reset the signal
  int signum = pending_host_signal;
  pending_host_signal = 0;

  // Get the PID of the process currently controlling the terminal.
  pid_t fg_pid = k_get_terminal_pgrp_id();

  // Ignore signal if invalid or targeting init
  if (fg_pid == PID_INVALID || fg_pid == PID_INIT) {
    return;
  }

  // Map host signal to PennOS signal
  int signal_num;
  if (signum == HOST_SIGINT) {
    // Ctrl-C maps to P_SIGTERM (signal 0)
    signal_num = 0;
  } else if (signum == HOST_SIGTSTP) {
    // Ctrl-Z maps to P_SIGSTOP (signal 1)
    signal_num = 1;
  } else {
    return;  // Ignore other signals
  }

  // Relay the PennOS signal
  s_kill(fg_pid, signal_num);
}

void setup_host_signals(void) {
  struct sigaction sa;

  // Manually zero-initialize the structure
  memset(&sa, 0, sizeof(sa));

  // 1. Set up the host_sigaction structure
  sa.sa_handler = host_sig_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  // 2. Register handler for Ctrl-C (HOST_SIGINT)
  if (sigaction(HOST_SIGINT, &sa, NULL) == -1) {
    perror("setup_host_signals: failed to set SIGINT handler");
  }

  // 3. Register handler for Ctrl-Z (HOST_SIGTSTP)
  if (sigaction(HOST_SIGTSTP, &sa, NULL) == -1) {
    perror("setup_host_signals: failed to set SIGTSTP handler");
  }

  // 4. Register handler for Ctrl-\ (HOST_SIGQUIT)
  if (sigaction(HOST_SIGQUIT, &sa, NULL) == -1) {
    perror("setup_host_signals: failed to set SIGQUIT handler");
  }
}
