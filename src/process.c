#include "process.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "./util/job.h"
#include "./util/p_errno.h"
#include "./util/p_handler.h"
#include "./util/p_signal.h"
#include "./util/parser.h"
#include "./util/queue.h"
#include "./util/spthread.h"
#include "./util/stress.h"
#include "fat_syscalls.h"
#include "scheduler.h"
#include "syscall.h"
#include "user_function.h"

#define PROMPT "$ "
#define MAX_LINE_LEN 4096
typedef void* (*program_entry_fn)(void*);  // Program entry point signature

static process_table pcb_table;
static pid_t next_pid = 1;
static pid_t g_terminal_pgrp_id = PID_INVALID;
static volatile bool g_shutdown_requested = false;

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Declarations =================== /////
////////////////////////////////////////////////////////////////////////////

/**
 * @brief Parses and executes a single command line.
 *
 * This function parses the input line, handles shell built-in commands
 * (nice, man, nice_pid, bg, fg, jobs, logout) directly, or spawns a new process
 * for other commands. It manages foreground and background execution,
 * job control updates, and process waiting.
 *
 * @param line The command line string to parse and execute.
 * @return Returns 0 on success.
 */
static int run_command_line(char* line);

/**
 * @brief Runs the shell in script mode.
 *
 * Opens the specified script file, reads it line by line, and executes
 * each line using run_command_line.
 *
 * @param script_name The path to the script file to execute.
 * @return NULL (typically called in a thread context).
 */
static void* shell_run_script(const char* script_name);

/**
 * @brief Runs the shell in interactive mode.
 *
 * Enters a loop that displays a prompt, reads user input from stdin,
 * and executes commands. It also handles background job notifications
 * (completed or stopped jobs) before displaying the prompt.
 *
 * @return NULL (typically called in a thread context).
 */
static void* shell_run_interactive(void);

/**
 * @brief Compares two strings.
 *
 * A helper function to compare two null-terminated strings.
 *
 * @param s1 The first string.
 * @param s2 The second string.
 * @return An integer less than, equal to, or greater than zero if s1 is found,
 *         respectively, to be less than, to match, or be greater than s2.
 */
static int str_compare(const char* s1, const char* s2);

/**
 * @brief Retrieves the function pointer for a built-in program.
 *
 * Checks if the given command name corresponds to a built-in program
 * (like cat, echo, ls, etc.) and returns the corresponding function pointer.
 *
 * @param command_name The name of the command to look up.
 * @return A pointer to the program's entry function, or NULL if not found.
 */
static program_entry_fn get_built_in_program(const char* command_name);

//////////////////////////////////////////////////////////////////////////////
// =================== Public API Implementation =================== ////////
////////////////////////////////////////////////////////////////////////////

void* shell_main(void* arg) {
  char** argv = (char**)arg;

  if (argv != NULL && argv[1] != NULL) {
    return shell_run_script(argv[1]);
  } else {
    return shell_run_interactive();
  }
}

pcb_t* k_proc_create(pcb_t* parent) {
  pcb_t* new_pcb = (pcb_t*)malloc(sizeof(struct pcb));
  if (!new_pcb) {
    perror("k_proc_create");
    return NULL;
  }

  pcb_init(new_pcb);
  new_pcb->pid = next_pid++;
  if (parent) {
    new_pcb->ppid = parent->pid;  // if no parent then ppid = 0
    new_pcb->parent = parent;
    vec_push_back(&parent->childs, new_pcb);  // update parent's child array.

    // Inherit file descriptors from parent (especially stdin/stdout/stderr)
    for (int i = 0; i < MAX_FD; i++) {
      new_pcb->fd_table[i] = parent->fd_table[i];
    }
  }

  pcb_table[new_pcb->pid] = new_pcb;  // add process to global pcb table
  // Note: CREATE log is now done in s_spawn after cmd_name is set
  return new_pcb;
}

void k_proc_cleanup(pcb_t* proc) {
  if (proc->pid != PID_INIT) {
    // Remove process from its parent's child list (if it had one)
    if (proc->parent) {
      vec_remove(&proc->parent->childs, proc);
    }
    // Note: k_adopt_orphans is now called in k_terminate, not here
    // This ensures orphans are adopted immediately when parent becomes zombie
  }

  // Wait for the thread to finish and free its resources (spthread_meta_t)
  spthread_join(proc->process, NULL);

  // Remove the process from global pcb table
  pcb_table[proc->pid] = NULL;

  if (proc->args != NULL) {
    for (int i = 0; proc->args[i] != NULL; i++) {
      free(proc->args[i]);
    }
    free(proc->args);
    proc->args = NULL;
  }

  // Deallocate memories
  vec_destroy(&proc->childs);
  free(proc);
}

void k_terminate(pcb_t* proc) {
  if (!proc || proc->state == P_ZOMBIE) {
    return;
  }

  // Log SIGNALED event if process was terminated by signal
  if (proc->exit_status == P_EXIT_SIGNALED) {
    k_log_event("SIGNALED", proc);
    // Cancel the process to prevent it from running
    spthread_cancel(proc->process);
  }

  // Remove process from any queue it might be in before changing state
  k_remove_from_queues(proc);

  proc->state = P_ZOMBIE;
  // Log the zombie event
  k_log_event("ZOMBIE", proc);

  // Adopt orphans immediately when process becomes zombie
  // This ensures children are transferred to init right away
  if (proc->pid != PID_INIT) {
    k_adopt_orphans(proc);
  }

  pcb_t* parent = proc->parent;
  if (parent && parent->state == P_BLOCKED && parent->wake_tick == 0) {
    k_unblock(parent);
  }

  // if (proc == current) {
  //   k_scheduler_tick();
  // }
}

void k_reap_zombie(pcb_t* proc, pid_t childpid) {
  if (!proc)
    return;

  for (int i = 0; i < vec_len(&proc->childs); i++) {
    pcb_t* child = vec_get(&proc->childs, i);

    if (child->pid == childpid && child->state == P_ZOMBIE) {
      vec_erase(&proc->childs, i);
      k_log_event("WAITED", child);
      k_proc_cleanup(child);
      break;
    }
  }
}

void k_adopt_orphans(pcb_t* proc) {
  pcb_t* init = pcb_table[PID_INIT];
  bool has_zombie = false;

  // Reassign the process's childs to process init
  for (int i = 0; i < vec_len(&proc->childs); i++) {
    pcb_t* child = vec_get(&proc->childs, i);
    child->parent = init;
    child->ppid = PID_INIT;
    vec_push_back(&init->childs, child);
    k_log_event("ORPHAN", child);

    // Check if this orphan is already a zombie
    if (child->state == P_ZOMBIE) {
      has_zombie = true;
    }
  }

  // If any zombie children were adopted, wake up init to reap them
  if (has_zombie && init->state == P_BLOCKED && init->wake_tick == 0) {
    k_unblock(init);
  }
}

void k_start_init_process(void) {
  pcb_t* init =
      k_proc_create(NULL);  // NULL parent means this is the root process
  if (!init) {
    return;
  }
  init->prio = 0;  // additional: INIT runs with priority 0

  // Set init process name
  snprintf(init->cmd_name, MAX_NAME_LEN, "init");

  // Initialize standard file descriptors for init process
  // These point to the global file descriptor table entries (0, 1, 2)
  // which were initialized by k_gdt_init() during mount()
  init->fd_table[0] = 0;  // STDIN
  init->fd_table[1] = 1;  // STDOUT
  init->fd_table[2] = 2;  // STDERR

  // Log the CREATE event now that cmd_name is set
  k_log_event("CREATE", init);

  // Create spthread for INIT process
  // Shell will be created in k_INIT_main
  int ret = spthread_create(&init->process, NULL, k_INIT_main, NULL);
  if (ret != 0) {
    k_proc_cleanup(init);
    return;
  }

  // Note: enqueueing should normally be taken care of in s_spawn. INIT is
  // launched on boot and is therefore case-specific.
  k_enqueue(init);
}

void* k_INIT_main(void* arg) {
  (void)arg;  // Unused parameter

  // Set up signal handlers for Ctrl-C and Ctrl-Z
  setup_host_signals();

  // Initialize the job table
  jobs_init();

  // Step 2: Spawn the shell process
  // Create shell with stdin (fd0) and stdout (fd1)
  char* shell_argv[] = {"shell", NULL};
  pid_t shell_pid = s_spawn(shell_main, shell_argv, NULL, NULL, 0);

  // Step 2.1: Check if shell spawn failed
  if (shell_pid < 0) {
    const char* msg = "init: failed to spawn shell\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }

  // Step 2.2: Set shell priority to 0 (interactive process)
  s_nice(shell_pid, 0);

  // Step 3: Main event loop - wait for child processes
  while (1) {
    // Check if shutdown has been requested (e.g., by logout command)
    if (k_is_shutdown_requested()) {
      const char* msg = "Shutdown requested. Terminating PennOS...\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      s_exit();
      return NULL;
    }

    int wstatus = 0;
    // Wait for any child process to change state (blocking call)
    pid_t waited_pid = s_waitpid(-1, &wstatus, false);

    // Check again after waitpid returns
    if (k_is_shutdown_requested()) {
      const char* msg = "Shutdown requested. Terminating PennOS...\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      s_exit();
      return NULL;
    }

    // Step 4: Handle shell process exit
    if (waited_pid == shell_pid) {
      // Shell exited - restart it for crash recovery
      // Note: Could check exit status to distinguish between
      //       crash (restart) vs logout (different handling)

      shell_pid = s_spawn(shell_main, shell_argv, NULL, NULL, 0);
      s_nice(shell_pid, 0);

      // Step 4.1: Check if restart failed
      if (shell_pid < 0) {
        const char* msg = "init: failed to restart shell\n";
        s_write(STDERR_FILENO, msg, strlen(msg));
        s_exit();
        return NULL;
      }
    }
    // Step 5: Handle other child processes (orphans/zombies)
    else if (waited_pid > 0) {
      // Reap zombie/orphan child process
      k_reap_zombie(pcb_table[PID_INIT], waited_pid);
    }
  }

  return NULL;
}

// Helper functions for system calls to access kernel state
pcb_t* get_current_process(void) {
  return current;
}

pcb_t* get_process_by_pid(pid_t pid) {
  if (pid < 0 || pid >= MAX_PROC) {
    return NULL;
  }
  return pcb_table[pid];
}

pid_t k_getpid(void) {
  pcb_t* current_proc = get_current_process();
  if (current_proc) {
    // Retrieve the pid from the PCB structure
    return current_proc->pid;
  }
  return PID_INVALID;  // PID_INVALID is 0, defined in struct.h
}

void k_set_terminal_pgrp_id(pid_t pid) {
  // Perform validation
  if (pid >= PID_INIT || pid == PID_INVALID) {
    g_terminal_pgrp_id = pid;
  }
}

pid_t k_get_terminal_pgrp_id(void) {
  return g_terminal_pgrp_id;
}

void k_request_shutdown(void) {
  g_shutdown_requested = true;
}

bool k_is_shutdown_requested(void) {
  return g_shutdown_requested;
}

void k_kill_all_processes(void) {
  // 1. Cancel all non-zombie processes
  for (int i = 0; i < MAX_PROC; i++) {
    if (pcb_table[i] && pcb_table[i]->state != P_ZOMBIE) {
      spthread_cancel(pcb_table[i]->process);
    }
  }

  // 2. Break parent pointers to avoid invalid accesses during cleanup
  for (int i = 0; i < MAX_PROC; i++) {
    if (pcb_table[i]) {
      pcb_table[i]->parent = NULL;
    }
  }

  // 3. Cleanup all processes
  for (int i = 0; i < MAX_PROC; i++) {
    if (pcb_table[i]) {
      k_proc_cleanup(pcb_table[i]);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Implementations =================== //
////////////////////////////////////////////////////////////////////////////

static int run_command_line(char* line) {
  struct parsed_command* pcmd = NULL;

  // Parse Command
  if (parse_command(line, &pcmd) != 0 || pcmd == NULL) {
    if (pcmd) {
      free(pcmd);
    }
    return 0;
  }

  // Check if we have any commands
  if (pcmd->num_commands == 0 || pcmd->commands[0] == NULL ||
      pcmd->commands[0][0] == NULL) {
    free(pcmd);
    return 0;
  }

  // Check for shell built-in commands that run as shell subroutines
  char** argv = pcmd->commands[0];
  int priority = -1;  // flag for nice command

  if (strcmp(argv[0], "nice") == 0) {
    // nice <priority> <command> [args...]
    if (argv[1] == NULL || argv[2] == NULL) {
      const char* msg = "nice: usage: nice <priority> <command> [args...]\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      free(pcmd);
      return 0;
    }

    priority = atoi(argv[1]);
    if (priority < 0 || priority > 2) {
      const char* msg = "nice: invalid priority\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      free(pcmd);
      return 0;
    }

    argv = argv + 2;  // Skip the first two arguments (nice and priority)
  } else if (strcmp(argv[0], "man") == 0) {
    u_man(argv);
    free(pcmd);
    return 0;
  } else if (strcmp(argv[0], "nice_pid") == 0) {
    u_nice_pid(argv);
    free(pcmd);
    return 0;
  } else if (strcmp(argv[0], "bg") == 0) {
    u_bg(argv);
    free(pcmd);
    return 0;
  } else if (strcmp(argv[0], "fg") == 0) {
    u_fg(argv);
    free(pcmd);
    return 0;
  } else if (strcmp(argv[0], "jobs") == 0) {
    u_jobs(argv);
    free(pcmd);
    return 0;
  } else if (strcmp(argv[0], "logout") == 0) {
    free(pcmd);
    u_logout(argv);
    return 0;
  }

  // Execute Command (Spawn Child)
  program_entry_fn program_entry = get_built_in_program(argv[0]);

  // Build command name for job tracking
  char command_name[64];
  const char* cmd = argv[0];
  const char* arg = argv[1];

  if (arg != NULL) {
    snprintf(command_name, sizeof(command_name), "%s %s", cmd, arg);
  } else {
    snprintf(command_name, sizeof(command_name), "%s", cmd);
  }

  pid_t child_pid = -1;

  if (program_entry == NULL) {
    // Not a built-in command - try to execute as a script via sub-shell
    char* shell_argv[] = {"shell", argv[0], NULL};
    child_pid = s_spawn(shell_main, shell_argv, pcmd->stdin_file,
                        pcmd->stdout_file, pcmd->is_file_append);
  } else {
    // Built-in command
    child_pid = s_spawn(program_entry, argv, pcmd->stdin_file,
                        pcmd->stdout_file, pcmd->is_file_append);
  }

  if (child_pid > 0) {
    pcb_t* child_pcb = get_process_by_pid(child_pid);

    if (priority != -1) {  // if nice command, set priority
      s_nice(child_pid, priority);
    }

    if (!pcmd->is_background) {
      int wstatus;
      // Foreground Process: Terminal Control & Blocking Wait
      k_set_terminal_pgrp_id(child_pid);
      s_waitpid(child_pid, &wstatus, false);  // Blocking wait (false = block)
      if (P_WIFSTOPPED(wstatus)) {
        // Process was stopped, add it to the job table as STOPPED
        int job_id = jobs_add(child_pid, child_pcb, command_name);
        job_t* job = jobs_find_by_pid(child_pid);
        if (job) {
          job->state = JOB_STOPPED;
          char buf[128];
          int len = snprintf(buf, sizeof(buf), "\n[%d] Stopped %s\n", job_id,
                             command_name);
          s_write(STDOUT_FILENO, buf, len);
        }
      } else if (P_WIFSIGNALED(wstatus)) {
        s_write(STDOUT_FILENO, "\n", 1);  // Newline for Ctrl-C termination
      }
      // fg_pid will be reset to PID_INVALID at the start of next loop iteration
    } else {
      // Background Process
      // Add the process to the job table immediately
      int job_id = jobs_add(child_pid, child_pcb, command_name);
      job_t* job = jobs_find_by_pid(child_pid);
      if (job) {
        job->state = JOB_BACKGROUND;
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "[%d] %d\n", job_id, child_pid);
        s_write(STDOUT_FILENO, buf, len);
      }
      if (strcmp(argv[0], "cat") == 0) {
        s_kill(child_pid, 1);
      }
    }
  } else {
    // Spawn failed
    if (program_entry == NULL) {
      char buf[128];
      int len =
          snprintf(buf, sizeof(buf), "shell: command not found: %s\n", argv[0]);
      s_write(STDERR_FILENO, buf, len);
    }
  }

  // Free the parsed command
  free(pcmd);
  pcmd = NULL;
  return 0;
}

static void* shell_run_script(const char* script_name) {
  // Check execute permission first
  if (s_check_executable(script_name) < 0 && P_ERRNO != P_ENOENT) {
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "shell: permission denied: %s\n",
                       script_name);
    s_write(STDERR_FILENO, buf, len);
    s_exit();
    return NULL;
  }

  // Try to open the script file from PennFAT
  int fd = s_open(script_name, F_READ);
  if (fd < 0) {
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "shell: script not found: %s\n",
                       script_name);
    s_write(STDERR_FILENO, buf, len);
    s_exit();
    return NULL;
  }

  char buffer[4096];
  ssize_t bytes_read;

  // Read and execute the script line by line
  while ((bytes_read = s_read(fd, sizeof(buffer) - 1, buffer)) > 0) {
    buffer[bytes_read] = '\0';

    char* line_start = buffer;
    char* newline;

    // Process each line in the buffer
    while ((newline = strchr(line_start, '\n')) != NULL) {
      *newline = '\0';

      // Skip empty lines
      if (strlen(line_start) > 0) {
        run_command_line(line_start);
      }

      line_start = newline + 1;
    }
  }

  s_close(fd);
  s_exit();  // Script finished
  return NULL;
}

static void* shell_run_interactive(void) {
  char line[MAX_LINE_LEN];

  while (1) {
    // No foreground process while shell is waiting for input
    k_set_terminal_pgrp_id(PID_INVALID);

    // Synchronous Child Waiting (Reap all zombies)
    int wstatus;
    pid_t reaped_pid;
    // Non-blocking wait (-1 for any child, false for non-blocking)
    while ((reaped_pid = s_waitpid(-1, &wstatus, true)) > 0) {
      // Child reaped. Check its status and update the job table.
      job_t* job = jobs_find_by_pid(reaped_pid);
      if (job) {
        if (P_WIFEXITED(wstatus) || P_WIFSIGNALED(wstatus)) {
          // Process exited or terminated by signal
          char buf[128];
          int len = snprintf(buf, sizeof(buf), "[%d] Done %s\n", job->job_id,
                             job->cmd);
          s_write(STDOUT_FILENO, buf, len);
          jobs_remove(reaped_pid);
        } else if (P_WIFSTOPPED(wstatus)) {
          // Process was stopped (should not happen in this loop, but good for
          // safety)
          job->state = JOB_STOPPED;
          char buf[128];
          int len = snprintf(buf, sizeof(buf), "\n[%d] Stopped %s\n",
                             job->job_id, job->cmd);
          s_write(STDOUT_FILENO, buf, len);
        }
      }
    }

    // Prompt and Read Command
    s_write(STDOUT_FILENO, PROMPT, strlen(PROMPT));
    fflush(stdout);

    if (fgets(line, MAX_LINE_LEN, stdin) == NULL) {
      if (feof(stdin)) {
        // Shutdown on EOF (Ctrl-D)
        clearerr(stdin);
        s_shutdown();
        s_exit();
        return NULL;
      }
      // Handle interruption (ctrl-c/z: reprompt)
      clearerr(stdin);
      s_write(STDOUT_FILENO, "\n", 1);
      continue;
    }

    run_command_line(line);
  }
}

static int str_compare(const char* s1, const char* s2) {
  if (s1 == NULL || s2 == NULL) {
    return (s1 == s2) ? 0 : 1;
  }

  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }

  return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static program_entry_fn get_built_in_program(const char* command_name) {
  if (command_name == NULL) {
    return NULL;
  }

  // --- Process-Running Built-ins (Using the 'u_' prefix from user_function.h)
  // ---

  if (str_compare(command_name, "cat") == 0)
    return u_cat;
  else if (str_compare(command_name, "sleep") == 0)
    return u_sleep;
  else if (str_compare(command_name, "busy") == 0)
    return u_busy;
  else if (str_compare(command_name, "echo") == 0)
    return u_echo;
  else if (str_compare(command_name, "ls") == 0)
    return u_ls;
  else if (str_compare(command_name, "touch") == 0)
    return u_touch;
  else if (str_compare(command_name, "mv") == 0)
    return u_mv;
  else if (str_compare(command_name, "cp") == 0)
    return u_cp;
  else if (str_compare(command_name, "rm") == 0)
    return u_rm;
  else if (str_compare(command_name, "chmod") == 0)
    return u_chmod;
  else if (str_compare(command_name, "ps") == 0)
    return u_ps;
  else if (str_compare(command_name, "kill") == 0)
    return u_kill;
  else if (str_compare(command_name, "zombify") == 0)
    return u_zombify;
  else if (str_compare(command_name, "orphanify") == 0)
    return u_orphanify;
  else if (str_compare(command_name, "hang") == 0)
    return hang;
  else if (str_compare(command_name, "nohang") == 0)
    return nohang;
  else if (str_compare(command_name, "recur") == 0)
    return recur;
  else if (str_compare(command_name, "crash") == 0)
    return crash;

  return NULL;
}

pcb_t** get_all_process(void) {
  return pcb_table;
}
