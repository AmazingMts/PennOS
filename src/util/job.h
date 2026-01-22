/**
 * @file job.h
 * @brief Job control table for PennOS-style shell and process management.
 *
 * This header declares a small fixed-size job table used by the shell and
 * the kernel's job-control helpers. Each `job_t` records a mapping from
 * a shell-visible job id to a process `pid` and its PCB pointer. The
 * implementation provides helpers to add/find/remove jobs and to print
 * the job table for debugging.
 *
 * The job table lives in `src/util/` and is used by the shell and
 * `process`/`scheduler` subsystems to implement foreground/background
 * job control.
 */

#ifndef JOB_H
#define JOB_H

#include <stdbool.h>
#include "struct.h"

/** Maximum number of simultaneous jobs tracked by the shell. */
#define MAX_JOBS 256

/**
 * @brief Possible states for a job entry.
 */
typedef enum {
  JOB_RUNNING,    /**< The job's process is currently running (foreground or
                     background). */
  JOB_STOPPED,    /**< The job has been stopped (e.g., via SIGTSTP) and can be
                     resumed. */
  JOB_BACKGROUND, /**< The job is running in the background. */
  JOB_DONE        /**< The job has terminated and can be reaped/removed. */
} job_state_t;

/**
 * @brief Entry for a job in the job table.
 */
typedef struct job {
  int job_id;   /**< Small integer assigned by the job subsystem (1..N) for
                   user-facing job names. */
  pid_t pid;    /**< Process id associated with this job. */
  pcb_t* pcb;   /**< Pointer to the process control block for quick access to
                   process state. */
  char cmd[64]; /**< Short copy of the command line used to launch the job
                   (NUL-terminated). */
  job_state_t state; /**< Current job state. */
  bool used; /**< Boolean flag indicating this table slot is occupied. */
} job_t;

/* Initialization --------------------------------------------------------- */

/**
 * @brief Initialize the job table.
 *
 * Clears the job table memory and resets job ID counters.
 * Must be called before any other jobs_* helper.
 */
void jobs_init(void);

/* Add / find / remove --------------------------------------------------- */

/**
 * @brief Add a new job to the job table.
 *
 * Finds a free slot in the job table and initializes it with the provided
 * information.
 *
 * @param pid The process ID of the job.
 * @param pcb A pointer to the Process Control Block of the job.
 * @param cmd The command string associated with the job.
 * @return The assigned job ID on success, or -1 if the table is full.
 */
int jobs_add(pid_t pid, pcb_t* pcb, const char* cmd);

/**
 * @brief Find a job entry by its job ID.
 *
 * @param id The user-facing job ID to search for.
 * @return A pointer to the job entry if found, or NULL otherwise.
 */
job_t* jobs_find_by_id(int id);

/**
 * @brief Find a job entry by its process ID.
 *
 * @param pid The process ID to search for.
 * @return A pointer to the job entry if found, or NULL otherwise.
 */
job_t* jobs_find_by_pid(pid_t pid);

/**
 * @brief Find the most recently added job that is currently stopped.
 *
 * @return A pointer to the job entry if found, or NULL if no stopped jobs
 * exist.
 */
job_t* jobs_find_most_recent_stopped(void);

/**
 * @brief Find the most recent job that is either stopped or backgrounded.
 *
 * This is useful for implementing fg/bg builtins where the default
 * action applies to the most recent relevant job.
 *
 * @return A pointer to the job entry if found, or NULL otherwise.
 */
job_t* jobs_find_most_recent_stopped_or_background(void);

/**
 * @brief Remove a job from the table.
 *
 * Marks the job slot corresponding to the given PID as unused.
 *
 * @param pid The process ID of the job to remove.
 */
void jobs_remove(pid_t pid);

/* Utilities -------------------------------------------------------------- */

/**
 * @brief Print the job table.
 *
 * Iterates through the job table and prints the status of all active jobs.
 * Used by the 'jobs' built-in command.
 */
void jobs_print(void);

/**
 * @brief Get a pointer to the internal job table.
 *
 * @return A pointer to the array of job_t structures (size MAX_JOBS).
 */
job_t* jobs_get_table(void);

#endif /* JOB_H */
