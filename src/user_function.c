#include "user_function.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "./util/job.h"
#include "./util/p_errno.h"
#include "./util/p_signal.h"
#include "./util/struct.h"
#include "fat_syscalls.h"
#include "process.h"
#include "syscall.h"

void* u_sleep(void* arg) {
  char** argv = (char**)arg;

  if (argv == NULL || argv[1] == NULL) {
    const char* msg = "sleep: missing operand\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }

  int seconds = atoi(argv[1]);
  if (seconds <= 0) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "sleep: invalid time interval '%s'\n",
                       argv[1]);
    s_write(STDERR_FILENO, buf, len);
    s_exit();
    return NULL;
  }

  unsigned int ticks = seconds * 10;
  s_sleep(ticks);

  s_exit();
  return NULL;
}

void* u_ps(void* arg) {
  (void)arg;

  pcb_t** global_table =
      s_get_all_process();  // Retrieve the global process table to iterate
                            // through all processes

  char header[128];
  int len = snprintf(header, sizeof(header), "     %-6s %-6s %-4s %-6s %s\n",
                     "PID", "PPID", "PRI", "STAT", "CMD");
  s_write(STDOUT_FILENO, header, len);

  for (int i = 0; i < MAX_PROC; i++) {
    if (global_table[i] == NULL) {
      continue;
    }

    pcb_t* p = global_table[i];
    char state = '?';

    switch (p->state) {
      case P_READY:
        state = 'R';
        break;
      case P_RUNNING:
        state = 'R';
        break;
      case P_BLOCKED:
        state = 'B';
        break;
      case P_STOPPED:
        state = 'S';
        break;
      case P_ZOMBIE:
        state = 'Z';
        break;
    }

    const char* cmd_name = p->cmd_name;
    char line[256];
    if (p->state == P_ZOMBIE && cmd_name[0] != '\0') {
      len = snprintf(line, sizeof(line), "     %-6d %-6d %-4d %c      %s \n",
                     p->pid, p->ppid, p->prio, state, cmd_name);
    } else if (cmd_name[0] != '\0') {
      len = snprintf(line, sizeof(line), "     %-6d %-6d %-4d %c      %s\n",
                     p->pid, p->ppid, p->prio, state, cmd_name);
    } else {
      len = snprintf(line, sizeof(line),
                     "     %-6d %-6d %-4d %c      <unknown>\n", p->pid, p->ppid,
                     p->prio, state);
    }
    s_write(STDOUT_FILENO, line, len);
  }

  s_exit();
  return NULL;
}

void* u_kill(void* arg) {
  char** argv = (char**)arg;

  if (argv == NULL || argv[1] == NULL) {
    const char* msg = "kill: missing argument\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }

  int signal = P_SIGTERM;
  int idx = 1;

  if (argv[1][0] == '-') {
    if (strcmp(argv[1], "-term") == 0) {
      signal = P_SIGTERM;
    } else if (strcmp(argv[1], "-stop") == 0) {
      signal = P_SIGSTOP;
    } else if (strcmp(argv[1], "-cont") == 0) {
      signal = P_SIGCONT;
    } else {
      char buf[64];
      int len =
          snprintf(buf, sizeof(buf), "kill: invalid signal '%s'\n", argv[1]);
      s_write(STDERR_FILENO, buf, len);
      s_exit();
      return NULL;
    }
    idx++;
  }

  if (argv[idx] == NULL) {
    const char* msg = "kill: missing pid\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }

  while (argv[idx] != NULL) {
    pid_t target = atoi(argv[idx]);
    if (target <= 0) {
      char buf[64];
      int len =
          snprintf(buf, sizeof(buf), "kill: invalid pid '%s'\n", argv[idx]);
      s_write(STDERR_FILENO, buf, len);
    } else {
      if (s_kill(target, signal) < 0) {  // Invoke the kill system call to send
                                         // the signal to the target process
        u_perror("kill");
      }
    }
    idx++;
  }

  s_exit();
  return NULL;
}

void* u_cat(void* arg) {
  char** argv = (char**)arg;
  if (s_cat(argv) < 0) {
    u_perror("cat");
  }
  s_exit();
  return NULL;
}

void* u_echo(void* arg) {
  char** argv = (char**)arg;

  if (argv == NULL) {
    s_write(STDOUT_FILENO, "\n", 1);
    s_exit();
    return NULL;
  }

  for (int i = 1; argv[i] != NULL; i++) {
    if (i > 1)
      s_write(STDOUT_FILENO, " ", 1);
    s_write(STDOUT_FILENO, argv[i], strlen(argv[i]));
  }
  s_write(STDOUT_FILENO, "\n", 1);

  s_exit();
  return NULL;
}

void* u_busy(void* arg) {
  (void)arg;
  while (1) {
  }
  return NULL;
}

void* u_ls(void* arg) {
  char** argv = (char**)arg;
  const char* filename = NULL;
  if (argv != NULL && argv[1] != NULL) {
    filename = argv[1];
  }
  if (s_ls(filename) < 0) {
    u_perror("ls");
  }
  s_exit();
  return NULL;
}

void* u_touch(void* arg) {
  char** argv = (char**)arg;
  if (argv == NULL || argv[1] == NULL) {
    const char* msg = "touch: missing file operand\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }

  for (int i = 1; argv[i] != NULL; i++) {
    int fd = s_open(argv[i], 4);
    if (fd >= 0) {
      s_close(fd);
    } else {
      u_perror("touch");
    }
  }
  s_exit();
  return NULL;
}

void* u_mv(void* arg) {
  char** argv = (char**)arg;
  if (argv == NULL || argv[1] == NULL || argv[2] == NULL) {
    const char* msg = "mv: missing operand\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }
  if (s_mv(argv[1], argv[2]) < 0) {
    u_perror("mv");
  }
  s_exit();
  return NULL;
}

void* u_cp(void* arg) {
  char** argv = (char**)arg;
  if (s_cp(argv) < 0) {
    u_perror("cp");
  }
  s_exit();
  return NULL;
}

void* u_rm(void* arg) {
  char** argv = (char**)arg;
  if (argv == NULL || argv[1] == NULL) {
    const char* msg = "rm: missing operand\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }
  for (int i = 1; argv[i] != NULL; i++) {
    if (s_unlink(argv[i]) < 0) {
      u_perror("rm");
    }
  }
  s_exit();
  return NULL;
}

void* u_chmod(void* arg) {
  char** argv = (char**)arg;
  if (argv == NULL || argv[1] == NULL || argv[2] == NULL) {
    const char* msg = "chmod: missing operand\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    s_exit();
    return NULL;
  }

  char* mode_str = argv[1];
  char* fname = argv[2];
  int mode = 0;

  if (mode_str[0] == '+' || mode_str[0] == '-' ||
      mode_str[0] == '=') {  // Check for symbolic mode (e.g., +x, -w)
    int mask = 0;
    for (int i = 1; mode_str[i] != '\0'; i++) {
      switch (mode_str[i]) {
        case 'r':
          mask |= 4;
          break;
        case 'w':
          mask |= 2;
          break;
        case 'x':
          mask |= 1;
          break;
        default: {
          char buf[64];
          int len = snprintf(buf, sizeof(buf), "chmod: invalid mode: %c\n",
                             mode_str[i]);
          s_write(STDERR_FILENO, buf, len);
          s_exit();
          return NULL;
        }
      }
    }

    if (mode_str[0] == '+') {
      mode = 0x80 | mask;  // Set bits: 0x80 flag indicates 'add' operation
    } else if (mode_str[0] == '-') {
      mode = 0x40 | mask;  // Clear bits: 0x40 flag indicates 'remove' operation
    } else {
      mode = 0x20 | mask;  // Set exact: 0x20 flag indicates 'set' operation
    }
  } else {
    if (strspn(mode_str, "01234567") != strlen(mode_str)) {
      char buf[64];
      int len =
          snprintf(buf, sizeof(buf), "chmod: invalid mode: '%s'\n", mode_str);
      s_write(STDERR_FILENO, buf, len);
      s_exit();
      return NULL;
    }
    mode = atoi(mode_str);
  }

  if (s_chmod(fname, mode) < 0) {
    u_perror("chmod");
  }
  s_exit();
  return NULL;
}

void* u_zombie_child(void* arg) {
  (void)arg;
  s_exit();
  return NULL;
}

void* u_zombify(void* arg) {
  (void)arg;

  char* child_argv[] = {"zombie_child", NULL};
  s_spawn(u_zombie_child, child_argv, NULL, NULL, 0);

  while (1) {
  }

  return NULL;
}

void* u_orphan_child(void* arg) {
  (void)arg;
  while (1) {
  }
  return NULL;
}

void* u_orphanify(void* arg) {
  (void)arg;

  char* child_argv[] = {"orphan_child", NULL};
  s_spawn(u_orphan_child, child_argv, NULL, NULL, 0);

  s_exit();
  return NULL;
}
void* u_nice_pid(void* arg) {
  char** argv = (char**)arg;

  if (argv == NULL || argv[1] == NULL || argv[2] == NULL) {
    const char* msg = "nice_pid: usage: nice_pid <priority> <pid>\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    return NULL;
  }

  int priority = atoi(argv[1]);
  if (priority < 0 || priority > 2) {
    const char* msg = "nice_pid: invalid priority\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    return NULL;
  }

  pid_t pid = atoi(argv[2]);
  if (pid <= 0) {
    const char* msg = "nice_pid: invalid pid\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
    return NULL;
  }

  if (s_nice(pid, priority) < 0) {
    const char* msg = "nice_pid: failed to set priority\n";
    s_write(STDERR_FILENO, msg, strlen(msg));
  }

  return NULL;
}

void* u_man(void* arg) {
  (void)arg;
  const char* help_text =
      "PennOS Shell Commands:\n\n"
      "Process Management:\n"
      "  ps                        - List all processes\n"
      "  kill <signal> <pid> ...   - Send signal to process (default: -term)\n"
      "  nice <pri> <cmd>          - Run command with priority (0-2)\n"
      "  nice_pid <pri> <pid>      - Change priority of existing process\n"
      "  sleep <seconds>           - Sleep for specified seconds\n"
      "  busy                      - Busy wait indefinitely\n\n"
      "File System:\n"
      "  cat <file> ...            - Concatenate and print files\n"
      "  ls [file]                 - List directory contents\n"
      "  touch <file> ...          - Create empty files or update timestamps\n"
      "  mv <src> <dst>            - Move/rename file\n"
      "  cp <src> <dst>            - Copy file (use -h for host, -a for "
      "append)\n"
      "  rm <file> ...             - Remove files\n"
      "  chmod <mode> <file>       - Change file permissions\n\n"
      "Job Control:\n"
      "  jobs                      - List active jobs\n"
      "  bg [job_id]               - Run a stopped job in background\n"
      "  fg [job_id]               - Bring a job to foreground\n\n"
      "Other:\n"
      "  echo <text>               - Echo text to stdout\n"
      "  zombify                   - Create zombie process (for testing)\n"
      "  orphanify                 - Create orphan process (for testing)\n"
      "  logout                    - Exit shell and shutdown PennOS\n"
      "  man                       - Show this help menu\n";

  s_write(STDOUT_FILENO, help_text, strlen(help_text));
  return NULL;
}

void* u_bg(void* arg) {
  char** argv = (char**)arg;
  job_t* job = NULL;

  if (argv != NULL && argv[1] != NULL) {
    int job_id = atoi(argv[1]);
    if (job_id <= 0) {
      const char* msg = "bg: argument must be a job ID\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      return NULL;
    }
    job = jobs_find_by_id(job_id);
    if (job == NULL) {
      const char* msg = "bg: no such job\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      return NULL;
    }
  } else {
    job = jobs_find_most_recent_stopped();
    if (job == NULL) {
      const char* msg = "bg: no stopped jobs\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      return NULL;
    }
  }

  if (job->state == JOB_RUNNING || job->state == JOB_BACKGROUND) {
    char msg[256];
    int len =
        snprintf(msg, sizeof(msg), "[%d] %s already running in background\n",
                 job->job_id, job->cmd);
    s_write(STDOUT_FILENO, msg, len);
    job->state = JOB_BACKGROUND;
    return NULL;
  }

  if (job->state == JOB_STOPPED) {
    job->state = JOB_BACKGROUND;
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "[%d] %s\n", job->job_id, job->cmd);
    s_write(STDOUT_FILENO, msg, len);

    if (s_kill(job->pid, 2) <
        0) {  // Send SIGCONT (2) to resume the stopped job in the background
      u_perror("bg: failed to continue process");
    }
  }
  return NULL;
}

void* u_fg(void* arg) {
  char** argv = (char**)arg;
  job_t* job = NULL;

  if (argv != NULL && argv[1] != NULL) {
    int job_id = atoi(argv[1]);
    if (job_id <= 0) {
      const char* msg = "fg: argument must be a job ID\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      return NULL;
    }
    job = jobs_find_by_id(job_id);
    if (job == NULL) {
      const char* msg = "fg: no such job\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      return NULL;
    }
  } else {
    job = jobs_find_most_recent_stopped_or_background();
    if (job == NULL) {
      const char* msg = "fg: no current job\n";
      s_write(STDERR_FILENO, msg, strlen(msg));
      return NULL;
    }
  }

  job->state = JOB_RUNNING;
  char msg[256];
  int len = snprintf(msg, sizeof(msg), "%s\n", job->cmd);
  s_write(STDOUT_FILENO, msg, len);

  if (job->pcb->state == P_STOPPED) {
    if (s_kill(job->pid, 2) < 0) {
      u_perror("fg: failed to continue process");
    }
  }

  int wstatus;
  k_set_terminal_pgrp_id(
      job->pid);  // Transfer terminal control to the job's process group
  s_waitpid(
      job->pid, &wstatus,
      false);  // Block and wait for the job to change state (stop or exit)
  if (P_WIFSTOPPED(wstatus)) {  // Check if the child process was stopped (e.g.,
                                // by Ctrl-Z)
    job->state = JOB_STOPPED;
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "\n[%d] Stopped %s\n", job->job_id,
                       job->cmd);
    s_write(STDOUT_FILENO, buf, len);
  } else if (P_WIFSIGNALED(wstatus)) {  // Check if the child process was
                                        // terminated by a signal
    jobs_remove(job->pid);
    s_write(STDOUT_FILENO, "\n", 1);
  } else if (P_WIFEXITED(
                 wstatus)) {  // Check if the child process exited normally
    jobs_remove(job->pid);
  }

  k_set_terminal_pgrp_id(
      PID_INVALID);  // Reclaim terminal control for the shell
  return NULL;
}

void* u_jobs(void* arg) {
  (void)arg;

  job_t* job_table = jobs_get_table();
  for (int i = 0; i < MAX_JOBS; i++) {
    if (!job_table[i].used)
      continue;

    const char* st = (job_table[i].state == JOB_RUNNING)      ? "Running"
                     : (job_table[i].state == JOB_STOPPED)    ? "Stopped"
                     : (job_table[i].state == JOB_BACKGROUND) ? "Background"
                                                              : "Done";

    char line[256];
    int len =
        snprintf(line, sizeof(line), "[%d] %-2d %-12s %s\n",
                 job_table[i].job_id, job_table[i].pid, st, job_table[i].cmd);
    s_write(STDOUT_FILENO, line, len);
  }

  return NULL;
}

void* u_logout(void* arg) {
  (void)arg;
  const char* msg = "Logging out...\n";
  s_write(STDOUT_FILENO, msg, strlen(msg));
  s_shutdown();
  // After shutdown is requested, exit this process
  s_exit();
  return NULL;
}
