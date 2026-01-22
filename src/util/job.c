#include "job.h"
#include <stdint.h>  // standard integer types
#include <stdio.h>
#include <string.h>
#include "fat_syscalls.h"

static job_t job_table[MAX_JOBS];
static int next_job_id = 1;

/* simple memset replacement */
static void memclr(void* ptr, int size) {
  unsigned char* p = (unsigned char*)ptr;
  for (int i = 0; i < size; i++)
    p[i] = 0;
}

/* safe manual string copy (no string.h) */
static void str_copy(char* dst, const char* src, int max) {
  int i = 0;
  while (src[i] != '\0' && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/* Helper function to find the most recent job by state */
static job_t* find_most_recent_by_state(job_state_t target_state) {
  int best_id = -1;
  job_t* best = NULL;

  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_table[i].used && job_table[i].state == target_state) {
      if (job_table[i].job_id > best_id) {
        best_id = job_table[i].job_id;
        best = &job_table[i];
      }
    }
  }
  return best;
}

void jobs_init() {
  memclr(job_table, sizeof(job_table));
}

int jobs_add(pid_t pid, pcb_t* pcb, const char* cmd) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (!job_table[i].used) {
      job_table[i].used = 1;
      job_table[i].pid = pid;
      job_table[i].pcb = pcb;
      job_table[i].state = JOB_RUNNING;
      job_table[i].job_id = next_job_id++;

      /* copy command name manually */
      str_copy(job_table[i].cmd, cmd, sizeof(job_table[i].cmd));

      return job_table[i].job_id;
    }
  }
  return -1;  // full
}

job_t* jobs_find_by_id(int id) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_table[i].used && job_table[i].job_id == id) {
      return &job_table[i];
    }
  }
  return NULL;
}

job_t* jobs_find_by_pid(pid_t pid) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_table[i].used && job_table[i].pid == pid) {
      return &job_table[i];
    }
  }
  return NULL;
}

job_t* jobs_find_most_recent_stopped() {
  return find_most_recent_by_state(JOB_STOPPED);
}

job_t* jobs_find_most_recent_stopped_or_background() {
  job_t* job = find_most_recent_by_state(JOB_STOPPED);
  if (job)
    return job;
  return find_most_recent_by_state(JOB_BACKGROUND);
}

void jobs_remove(pid_t pid) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_table[i].used && job_table[i].pid == pid) {
      job_table[i].pcb = NULL;
      job_table[i].used = 0;
      job_table[i].pid = 0;
      job_table[i].job_id = -1;
      return;  // Job found and removed
    }
  }
}

void jobs_print() {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (!job_table[i].used)
      continue;

    const char* st = (job_table[i].state == JOB_RUNNING)      ? "Running"
                     : (job_table[i].state == JOB_STOPPED)    ? "Stopped"
                     : (job_table[i].state == JOB_BACKGROUND) ? "Background"
                                                              : "Done";

    char buf[512];
    int len = snprintf(buf, sizeof(buf), "[%d] %d %-10s %s\n", job_table[i].job_id,
                       job_table[i].pid, st, job_table[i].cmd);
    s_write(STDOUT_FILENO, buf, len);
  }
}

job_t* jobs_get_table() {
  return job_table;
}
