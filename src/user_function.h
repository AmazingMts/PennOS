/**
 * @file user_function.h
 * @brief User-level shell command implementations for PennOS.
 *
 * This header defines the interface for various shell commands that can be
 * executed within the PennOS kernel environment. Commands are implemented as
 * thread functions (taking void* arg and returning void*) to allow execution
 * as separate processes or shell subroutines.
 */

#ifndef USER_FUNCTION_H
#define USER_FUNCTION_H

/**
 * @brief Sleep for a specified number of seconds.
 * @param arg Pointer to argument string containing sleep duration.
 * @return NULL on success, or error code on failure.
 */
void* u_sleep(void* arg);

/**
 * @brief List all running processes with their details.
 * @param arg Unused argument pointer.
 * @return NULL on success.
 */
void* u_ps(void* arg);

/**
 * @brief Send a signal to terminate a process by PID.
 * @param arg Pointer to argument string containing target PID.
 * @return NULL on success, or error code on failure.
 */
void* u_kill(void* arg);

/**
 * @brief Display the contents of one or more files.
 * @param arg Pointer to argument string containing file path(s).
 * @return NULL on success, or error code on failure.
 */
void* u_cat(void* arg);

/**
 * @brief Print text to standard output.
 * @param arg Pointer to argument string containing text to echo.
 * @return NULL on success.
 */
void* u_echo(void* arg);

/**
 * @brief Busy-wait loop for CPU load testing.
 * @param arg Unused argument pointer.
 * @return NULL when interrupted.
 */
void* u_busy(void* arg);

/**
 * @brief List directory contents.
 * @param arg Pointer to argument string containing directory path (optional).
 * @return NULL on success, or error code on failure.
 */
void* u_ls(void* arg);

/**
 * @brief Create empty files or update modification times.
 * @param arg Pointer to argument string containing file path(s).
 * @return NULL on success, or error code on failure.
 */
void* u_touch(void* arg);

/**
 * @brief Move or rename files.
 * @param arg Pointer to argument string containing source and destination
 * paths.
 * @return NULL on success, or error code on failure.
 */
void* u_mv(void* arg);

/**
 * @brief Copy files.
 * @param arg Pointer to argument string containing source and destination
 * paths.
 * @return NULL on success, or error code on failure.
 */
void* u_cp(void* arg);

/**
 * @brief Remove (delete) files.
 * @param arg Pointer to argument string containing file path(s).
 * @return NULL on success, or error code on failure.
 */
void* u_rm(void* arg);

/**
 * @brief Change file permissions.
 * @param arg Pointer to argument string containing permissions and file path.
 * @return NULL on success, or error code on failure.
 */
void* u_chmod(void* arg);

/**
 * @brief Create a zombie process for testing.
 * @param arg Unused argument pointer.
 * @return NULL after spawning zombie child.
 */
void* u_zombify(void* arg);

/**
 * @brief Child process that exits immediately to become a zombie.
 * @param arg Unused argument pointer.
 * @return NULL (exit code).
 */
void* u_zombie_child(void* arg);

/**
 * @brief Create an orphan process for testing.
 * @param arg Unused argument pointer.
 * @return NULL after spawning orphan child.
 */
void* u_orphanify(void* arg);

/**
 * @brief Child process that continues running after parent exits.
 * @param arg Unused argument pointer.
 * @return NULL after long-running loop.
 */
void* u_orphan_child(void* arg);
/**
 * @brief Change priority of a specific process by PID.
 * @param arg Pointer to argument string containing PID and priority value.
 * @return NULL on success, or error code on failure.
 */
void* u_nice_pid(void* arg);

/**
 * @brief Display help information for shell commands.
 * @param arg Pointer to argument string containing command name (optional).
 * @return NULL on success.
 */
void* u_man(void* arg);

/**
 * @brief Move a process to background execution.
 * @param arg Pointer to argument string containing job ID or command.
 * @return NULL on success, or error code on failure.
 */
void* u_bg(void* arg);

/**
 * @brief Move a background process to foreground execution.
 * @param arg Pointer to argument string containing job ID.
 * @return NULL on success, or error code on failure.
 */
void* u_fg(void* arg);

/**
 * @brief List all background jobs.
 * @param arg Unused argument pointer.
 * @return NULL on success.
 */
void* u_jobs(void* arg);

/**
 * @brief Exit the shell and terminate the session.
 * @param arg Unused argument pointer.
 * @return NULL (does not return on success).
 */
void* u_logout(void* arg);

#endif
