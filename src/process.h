#ifndef PROCESS_H
#define PROCESS_H

#include "./util/struct.h"

/**
 * @brief The entry point for the shell process.
 *
 * This function serves as the main entry point for the shell. It determines
 * whether to run in script mode (if a script file is provided) or interactive
 * mode.
 *
 * @param arg A pointer to the argument vector (argv). argv[1] is checked for a
 * script file.
 * @return NULL upon exit.
 */
void* shell_main(void* arg);

/**
 * @brief Allocates and initializes a new Process Control Block (PCB).
 *
 * This function allocates memory for a new PCB, initializes it with default
 * values, and sets up the parent-child relationship if a parent is provided. It
 * also inherits file descriptors from the parent.
 *
 * @param parent The parent process's PCB. Can be NULL for the root process
 * (init).
 * @return A pointer to the newly created PCB, or NULL on failure.
 */
pcb_t* k_proc_create(pcb_t* parent);

/**
 * @brief Cleans up and deallocates a process's resources.
 *
 * This function frees the memory associated with the PCB, including its
 * argument strings and child vector. It also joins the underlying thread. Note:
 * It does NOT handle orphan adoption; that is done in k_terminate.
 *
 * @param proc The PCB of the process to cleanup.
 */
void k_proc_cleanup(pcb_t* proc);

/**
 * @brief Terminates a process and transitions it to the ZOMBIE state.
 *
 * This function handles the logical termination of a process in the kernel.
 * It marks the process as ZOMBIE, handles orphan adoption (transferring
 * children to init), unblocks the parent if it is waiting, and logs the
 * termination event.
 *
 * @param proc The PCB of the process to terminate.
 */
void k_terminate(pcb_t* proc);

/**
 * @brief Reaps a specific zombie child process.
 *
 * Iterates through the parent's list of children to find the zombie child with
 * the specified PID. If found, it removes the child from the list, logs the
 * "WAITED" event, and cleans up the child's resources.
 *
 * @param proc The PCB of the parent process.
 * @param childpid The PID of the zombie child to reap.
 */
void k_reap_zombie(pcb_t* proc, pid_t childpid);

/**
 * @brief Adopts the children of a terminating process.
 *
 * Transfers all children of the given process to the init process.
 * If any adopted child is already a zombie, it wakes up init to reap them.
 *
 * @param proc The PCB of the process whose children are being orphaned.
 */
void k_adopt_orphans(pcb_t* proc);

/**
 * @brief Forces a shutdown by killing all processes.
 *
 * Cancels all running threads, breaks parent pointers to prevent invalid access
 * during cleanup, and deallocates all process resources.
 */
void k_kill_all_processes(void);

/**
 * @brief Initializes and starts the PID_INIT process.
 *
 * Creates the init process (PID 1), sets its priority to 0, initializes
 * standard file descriptors, and launches the k_INIT_main thread.
 */
void k_start_init_process(void);

/**
 * @brief The main loop for the init process.
 *
 * Sets up signal handlers, initializes the job system, spawns the shell,
 * and enters a loop to wait for and reap orphaned child processes.
 * It also handles shell restarts if the shell crashes.
 *
 * @param arg Unused argument.
 * @return NULL.
 */
void* k_INIT_main(void* arg);

/**
 * @brief Retrieves the PID of the current running process.
 *
 * @return The PID of the current process, or PID_INVALID if no process is
 * running.
 */
pid_t k_getpid(void);

/**
 * @brief Sets the process group ID that controls the terminal.
 *
 * Used for terminal job control to designate which process group is in the
 * foreground.
 *
 * @param pid The PID of the process (group) to set as foreground.
 */
void k_set_terminal_pgrp_id(pid_t pid);

/**
 * @brief Retrieves the process group ID that currently controls the terminal.
 *
 * @return The PID of the foreground process group.
 */
pid_t k_get_terminal_pgrp_id(void);

/**
 * @brief Sets the global shutdown flag to true.
 *
 * Signals the kernel (specifically init) to begin the shutdown sequence.
 */
void k_request_shutdown(void);

/**
 * @brief Checks if a system shutdown has been requested.
 *
 * @return true if shutdown is requested, false otherwise.
 */
bool k_is_shutdown_requested(void);

/**
 * @brief Retrieves the PCB of the currently running process.
 *
 * @return A pointer to the current process's PCB.
 */
pcb_t* get_current_process(void);

/**
 * @brief Retrieves a process PCB by its PID.
 *
 * @param pid The PID of the process to retrieve.
 * @return A pointer to the PCB, or NULL if the PID is invalid.
 */
pcb_t* get_process_by_pid(pid_t pid);

/**
 * @brief Retrieves the global process table.
 *
 * @return A pointer to the array of PCB pointers.
 */
pcb_t** get_all_process(void);

#endif
