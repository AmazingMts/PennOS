#include "syscall.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "./util/p_errno.h"
#include "./util/p_signal.h"
#include "./util/queue.h"
#include "./util/spthread.h"
#include "fat_kernel.h"
#include "fat_syscalls.h"
#include "process.h"
#include "scheduler.h"

// Define standard file descriptors if not already defined
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Declarations =================== /////
////////////////////////////////////////////////////////////////////////////

/**
 * @brief Helper function to handle I/O redirection before executing a process.
 *
 * This wrapper is used when a process is spawned with stdin or stdout
 * redirection. It sets up the file descriptors for the new process by opening
 * the specified input/output files and replacing the standard file descriptors
 * (0 and 1). After setup, it calls the original process entry function. It also
 * handles cleanup of allocated memory for filenames.
 *
 * @param arg A pointer to a spawn_wrapper_args_t structure containing the
 *            original function pointer, arguments, and redirection file paths.
 * @return The return value of the executed process function, or NULL on error.
 */
static void* spawn_wrapper(void* arg);

/**
 * @brief Cleanup function for the spawn wrapper.
 *
 * Restores the original stdin and stdout file descriptors.
 * Registers the cleanup handler with pthread_cleanup_push.
 *
 * @param arg A pointer to a spawn_wrapper_args_t structure containing the
 *            original function pointer, arguments, and redirection file paths.
 */
static void spawn_cleanup(void* arg);

/**
 * @brief Creates a deep copy of an argument vector (argv).
 *
 * Allocates memory for a new array of string pointers and copies each string
 * from the source argv. The new array is NULL-terminated.
 *
 * @param argv The null-terminated array of strings to copy.
 * @return A pointer to the new copy of argv, or NULL if memory allocation
 * fails. Sets P_ERRNO to P_ENOMEM on failure.
 */
static char** deep_copy_args(char** argv);

//////////////////////////////////////////////////////////////////////////////
// =================== Public API Implementation =================== ////////
////////////////////////////////////////////////////////////////////////////

pid_t s_spawn(void* (*func)(void*),
              char* argv[],
              const char* stdin_file,
              const char* stdout_file,
              int is_append) {
  // Get the current (parent) process
  pcb_t* parent = get_current_process();
  // Create a new child process
  pcb_t* child = k_proc_create(parent);
  if (!child) {
    P_ERRNO = P_ENOMEM;
    return -1;
  }
  child->prio = 1;

  // Set command name from argv[0]
  if (argv && argv[0]) {
    snprintf(child->cmd_name, MAX_NAME_LEN, "%s", argv[0]);
  } else {
    snprintf(child->cmd_name, MAX_NAME_LEN, "<unknown>");
  }

  // Log the CREATE event now that cmd_name is set
  k_log_event("CREATE", child);

  // Deep copy arguments
  if (argv != NULL) {
    child->args = deep_copy_args(argv);
    if (child->args == NULL) {
      k_proc_cleanup(child);
      return -1;  // P_ERRNO is set inside deep_copy_args
    }
  } else {
    child->args = NULL;
  }

  // Prepare wrapper arguments if redirection is needed
  void* (*thread_func)(void*) = func;
  void* thread_arg = (void*)child->args;

  if (stdin_file != NULL || stdout_file != NULL) {
    spawn_wrapper_args_t* wrapper_args = malloc(sizeof(spawn_wrapper_args_t));
    if (!wrapper_args) {
      k_proc_cleanup(child);
      perror("malloc");
      P_ERRNO = P_ENOMEM;
      return -1;
    }

    wrapper_args->func = func;
    wrapper_args->argv = child->args;
    wrapper_args->stdin_file = stdin_file ? strdup(stdin_file) : NULL;
    wrapper_args->stdout_file = stdout_file ? strdup(stdout_file) : NULL;
    wrapper_args->is_append = is_append;

    thread_func = spawn_wrapper;
    thread_arg = wrapper_args;
  }

  // Create the spthread for this process
  int ret = spthread_create(&child->process, NULL, thread_func, thread_arg);
  if (ret != 0) {
    k_proc_cleanup(child);
    P_ERRNO = P_ETHREAD;
    return -1;
  }
  child->state = P_READY;
  k_enqueue(child);
  return child->pid;
}

pid_t s_waitpid(pid_t pid, int* wstatus, bool nohang) {
  pcb_t* parent = get_current_process();
  if (!parent) {
    P_ERRNO = P_EINVAL;
    return -1;
  }

  // Check if parent has any children
  if (vec_len(&parent->childs) == 0) {
    P_ERRNO = P_ECHILD;
    return -1;
  }

  while (1) {
    // Look for a zombie child or a child that has changed state
    for (int i = 0; i < vec_len(&parent->childs); i++) {
      pcb_t* child = vec_get(&parent->childs, i);

      // If pid is -1, wait for any child
      // If pid is positive, wait for specific child
      if (pid != -1 && child->pid != pid) {
        continue;
      }

      // Check if child is a zombie (terminated)
      if (child->state == P_ZOMBIE) {
        pid_t child_pid = child->pid;

        // Set the wait status
        if (wstatus) {
          *wstatus = 0;
          switch (child->exit_status) {
            case P_EXIT_EXITED:
              *wstatus |= W_EXITED;  // Exited normally
              break;
            case P_EXIT_SIGNALED:
              *wstatus |= W_SIGNALED;  // Terminated by signal
              break;
            case P_EXIT_STOPPED:
              *wstatus |= W_STOPPED;  // Stopped
              break;
            default:
              break;
          }
        }

        // Reap the zombie
        k_reap_zombie(parent, child_pid);
        return child_pid;
      }

      // Check if child has been stopped (state change)
      if (child->state == P_STOPPED) {
        if (!child->stopped_reported) {
          if (wstatus) {
            *wstatus = W_STOPPED;  // Stopped
          }
          child->stopped_reported = true;
          return child->pid;
        }
      }
    }

    // If nohang is true, return immediately if no child has changed state
    if (nohang) {
      return 0;
    }

    // Block the parent process until a child changes state
    parent->wake_tick = 0;  // Wait indefinitely (not a timed sleep)
    k_block(parent);
    spthread_suspend_self();  // Actually suspend this thread

    // When we get here, we've been unblocked by a child state change
    // Loop again to find and reap the child
  }
}

int s_kill(pid_t pid, int signal) {
  if (pid == PID_INIT) {
    P_ERRNO = P_EPERM;
    return -1;
  }
  pcb_t* target = get_process_by_pid(pid);
  if (!target) {
    P_ERRNO = P_ESRCH;
    return -1;
  }

  // Map the signal number to PennOS signal type
  psignal_t psig;
  switch (signal) {
    case 0:  // P_SIGTERM
      psig = P_SIGTERM;
      target->exit_status = P_EXIT_SIGNALED;
      break;
    case 1:  // P_SIGSTOP
      psig = P_SIGSTOP;
      break;
    case 2:  // P_SIGCONT
      psig = P_SIGCONT;
      break;
    default:
      P_ERRNO = P_EINVAL;
      return -1;
  }

  k_signal_deliver(target, psig);

  return 0;
}

void s_exit(void) {
  pcb_t* current = get_current_process();
  if (!current) {
    return;
  }

  // Set exit status to normal exit
  current->exit_status = P_EXIT_EXITED;

  // Log the exit event
  k_log_event("EXITED", current);

  // Terminate the process (makes it a zombie)
  k_terminate(current);

  // The process is now a zombie and will be reaped by its parent
  // The scheduler will not schedule this process again
  spthread_exit(NULL);  // Exit the thread
}

int s_nice(pid_t pid, int priority) {
  // Validate priority range
  if (priority < 0 || priority >= NUM_PRIO) {
    P_ERRNO = P_EINVAL;
    return -1;
  }

  // Get the target process
  pcb_t* target = get_process_by_pid(pid);
  if (!target) {
    P_ERRNO = P_ESRCH;
    return -1;
  }

  // Set the new priority
  k_set_priority(target, priority);

  return 0;
}

void s_sleep(unsigned int ticks) {
  if (ticks == 0) {
    return;
  }

  pcb_t* proc = get_current_process();
  if (!proc) {
    return;
  }

  // Set the wake time for this process
  proc->wake_tick = tick + ticks;

  // Block the process and use a loop to prevent premature wakeup (e.g. from
  // signals like STOP/CONT) The process should only return when the sleep time
  // has actually elapsed or when the scheduler wakes it up (setting wake_tick
  // to 0).
  while (proc->wake_tick > 0 && tick < (uint64_t)proc->wake_tick) {
    k_block(proc);
    spthread_suspend_self();  // Actually suspend this thread
  }

  // When the process is unblocked by the scheduler after the sleep time,
  // execution will continue here
}

pid_t s_getpid(void) {
  // Directly call the kernel function to retrieve the PID.
  return k_getpid();
}

pcb_t** s_get_all_process(void) {
  return get_all_process();
}

void s_shutdown(void) {
  s_write(STDERR_FILENO, "Shutdown requested. PennOS will terminate.\n", 43);
  k_request_shutdown();
}

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Implementations =================== //
////////////////////////////////////////////////////////////////////////////

static void spawn_cleanup(void* arg) {
  spawn_wrapper_args_t* wrapper_args = (spawn_wrapper_args_t*)arg;
  pcb_t* current_proc = get_current_process();

  if (current_proc) {
    // Restore stdin
    if (wrapper_args->stdin_file) {
      if (current_proc->fd_table[STDIN_FILENO] >= 0 &&
          current_proc->fd_table[STDIN_FILENO] != wrapper_args->saved_stdin) {
        s_close(STDIN_FILENO);
      }
      current_proc->fd_table[STDIN_FILENO] = wrapper_args->saved_stdin;
    }

    // Restore stdout
    if (wrapper_args->stdout_file) {
      if (current_proc->fd_table[STDOUT_FILENO] >= 0 &&
          current_proc->fd_table[STDOUT_FILENO] != wrapper_args->saved_stdout) {
        s_close(STDOUT_FILENO);
      }
      current_proc->fd_table[STDOUT_FILENO] = wrapper_args->saved_stdout;
    }
  }

  // Free memory
  if (wrapper_args->stdin_file) free(wrapper_args->stdin_file);
  if (wrapper_args->stdout_file) free(wrapper_args->stdout_file);
  free(wrapper_args);
}

// Wrapper function that handles file redirection before calling the actual
// function
static void* spawn_wrapper(void* arg) {
  spawn_wrapper_args_t* wrapper_args = (spawn_wrapper_args_t*)arg;
  pcb_t* current_proc = get_current_process();

  if (!current_proc) {
    free(wrapper_args->stdin_file);
    free(wrapper_args->stdout_file);
    free(wrapper_args);
    return NULL;
  }

  // Check for input/output file conflict in append mode
  if (wrapper_args->stdin_file != NULL && wrapper_args->stdout_file != NULL &&
      wrapper_args->is_append &&
      strcmp(wrapper_args->stdin_file, wrapper_args->stdout_file) == 0) {
    s_write(STDERR_FILENO,
            "Error: Input and output files cannot be the same in append mode.\n",
            65);
    free(wrapper_args->stdin_file);
    free(wrapper_args->stdout_file);
    free(wrapper_args);
    s_exit();
    return NULL;
  }

  // Save original stdin/stdout for restoration
  wrapper_args->saved_stdin = current_proc->fd_table[STDIN_FILENO];
  wrapper_args->saved_stdout = current_proc->fd_table[STDOUT_FILENO];

  void* result = NULL;

  // Register cleanup handler
  pthread_cleanup_push(spawn_cleanup, wrapper_args);

  // Handle stdout redirection FIRST (to allow truncation if not append)
  if (wrapper_args->stdout_file != NULL) {
    int flags = wrapper_args->is_append ? F_APPEND : F_WRITE;
    int new_fd = s_open(wrapper_args->stdout_file, flags);
    if (new_fd >= 0) {
      // Move the new fd to stdout position if it's not already there
      if (new_fd != STDOUT_FILENO) {
        current_proc->fd_table[STDOUT_FILENO] = current_proc->fd_table[new_fd];
        current_proc->fd_table[new_fd] = -1;
      }
    } else {
      u_perror(wrapper_args->stdout_file);
      s_exit();
      return NULL;
    }
  }

  // Handle stdin redirection SECOND
  if (wrapper_args->stdin_file != NULL) {
    int new_fd = s_open(wrapper_args->stdin_file, F_READ);
    if (new_fd >= 0) {
      // Move the new fd to stdin position if it's not already there
      if (new_fd != STDIN_FILENO) {
        current_proc->fd_table[STDIN_FILENO] = current_proc->fd_table[new_fd];
        current_proc->fd_table[new_fd] = -1;
      }
    } else {
      u_perror(wrapper_args->stdin_file);
      s_exit();
      return NULL;
    }
  }

  // Call the actual function
  result = wrapper_args->func((void*)wrapper_args->argv);

  // We should not reach here, but the macro requires it to close some brackets
  // (I have no idea why)
  pthread_cleanup_pop(1);

  return result;
}

static char** deep_copy_args(char** argv) {
  if (argv == NULL) {
    return NULL;
  }

  int argc = 0;
  while (argv[argc] != NULL)
    argc++;

  char** new_args = malloc((argc + 1) * sizeof(char*));
  if (new_args == NULL) {
    P_ERRNO = P_ENOMEM;
    return NULL;
  }

  for (int i = 0; i < argc; i++) {
    new_args[i] = strdup(argv[i]);
    if (new_args[i] == NULL) {
      // Cleanup partially allocated args
      for (int j = 0; j < i; j++)
        free(new_args[j]);
      free(new_args);
      P_ERRNO = P_ENOMEM;
      return NULL;
    }
  }
  new_args[argc] = NULL;
  return new_args;
}
