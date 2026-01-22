#ifndef SYSCALL_H
#define SYSCALL_H

#include <sys/types.h>
#include "./util/struct.h"

/** @brief Macros for checking wait status */
#define W_EXITED (1 << 0)
#define W_SIGNALED (1 << 1)
#define W_STOPPED (1 << 2)

#define P_WIFEXITED(status) ((status) & W_EXITED)
#define P_WIFSTOPPED(status) ((status) & W_STOPPED)
#define P_WIFSIGNALED(status) ((status) & W_SIGNALED)

// Structure to pass redirection info to child process
typedef struct {
  void* (*func)(void*);
  char** argv;
  char* stdin_file;
  char* stdout_file;
  int is_append;
  int saved_stdin;
  int saved_stdout;
} spawn_wrapper_args_t;

/**
 * @brief Create a child process that executes the function `func`.
 * The child will retain some attributes of the parent.
 *
 * @param func Function to be executed by the child process.
 * @param argv Null-terminated array of args, including the command name as
 * argv[0].
 * @param stdin_file Input redirection file (NULL for no redirection).
 * @param stdout_file Output redirection file (NULL for no redirection).
 * @param is_append If true, append to output file; otherwise truncate.
 * @return pid_t The process ID of the created child process.
 */
pid_t s_spawn(void* (*func)(void*),
              char* argv[],
              const char* stdin_file,
              const char* stdout_file,
              int is_append);

/**
 * @brief Wait on a child of the calling process, until it changes state.
 * If `nohang` is true, this will not block the calling process and return
 * immediately.
 *
 * @param pid Process ID of the child to wait for.
 * @param wstatus Pointer to an integer variable where the status will be
 * stored.
 * @param nohang If true, return immediately if no child has exited.
 * @return pid_t The process ID of the child which has changed state on success,
 * -1 on error.
 */
pid_t s_waitpid(pid_t pid, int* wstatus, bool nohang);

/**
 * @brief Send a signal to a particular process.
 *
 * @param pid Process ID of the target proces.
 * @param signal Signal number to be sent.
 * @return 0 on success, -1 on error.
 */
int s_kill(pid_t pid, int signal);

/**
 * @brief Unconditionally exit the calling process.
 */
void s_exit(void);

/**
 * @brief Set the priority of the specified thread.
 *
 * @param pid Process ID of the target thread.
 * @param priority The new priority value of the thread (0, 1, or 2)
 * @return 0 on success, -1 on failure.
 */
int s_nice(pid_t pid, int priority);

/**
 * @brief Suspends execution of the calling process for a specified number of
 * clock ticks.
 *
 * This function is analogous to `sleep(3)` in Linux, with the behavior that the
 * system clock continues to tick even if the call is interrupted. The sleep can
 * be interrupted by a P_SIGTERM signal, after which the function will return
 * prematurely.
 *
 * @param ticks Duration of the sleep in system clock ticks. Must be greater
 * than 0.
 */
void s_sleep(unsigned int ticks);

/**
 * @brief User-level system call to get the calling process's PID.
 * * This function serves as the interface for PennOS programs to retrieve
 * their own unique Process ID.
 *
 * @return The PID of the calling process.
 */
pid_t s_getpid(void);

/**
 * @brief PennOS System Call: Open a file.
 * @param filename The name of the file to open.
 * @param flags The file open flags (F_READ, F_WRITE, F_APPEND).
 * @return The new file descriptor on success, or -1 on error (P_ERRNO set).
 */
int s_open(const char* filename, int flags);

/**
 * @brief PennOS System Call: Close a file descriptor.
 * @param fd The file descriptor to close.
 * @return 0 on success, or -1 on error (P_ERRNO set).
 */
int s_close(int fd);

/**
 * @brief Request immediate system shutdown (used by logout command)
 * This function sets a global flag that causes init to terminate PennOS.
 */
void s_shutdown(void);

/**
 * @brief Retrieves the global process table.
 *
 * This function allows user-level access to the process table for commands like
 * ps.
 *
 * @return A pointer to the array of PCB pointers.
 */
pcb_t** s_get_all_process(void);

/**
 * @brief Lists files in a directory.
 *
 * Lists all files in the specified directory (or current directory if filename
 * is NULL/empty) and writes the output to STDOUT.
 *
 * @param filename The name of the directory to list (or file to stat).
 * @return 0 on success, or -1 on error.
 */
int s_ls(const char* filename);

/**
 * @brief Deletes a file from the file system.
 *
 * Removes the directory entry for the specified file and frees its associated
 * blocks.
 *
 * @param fname The name of the file to unlink.
 * @return 0 on success, or -1 on error.
 */
int s_unlink(const char* fname);

/**
 * @brief Renames or moves a file.
 *
 * Changes the name of the source file to the destination name.
 *
 * @param src The current name of the file.
 * @param dest The new name of the file.
 * @return 0 on success, or -1 on error.
 */
int s_mv(const char* src, const char* dest);

/**
 * @brief Copies one or more files to a destination.
 *
 * @param args An array of arguments where args[0] is "cp", args[1]... are
 * sources, and the last argument is the destination.
 * @return 0 on success, or -1 on error.
 */
int s_cp(char** args);

/**
 * @brief Concatenates and displays file contents.
 *
 * Reads files sequentially and writes them to standard output. If no files are
 * provided, reads from standard input.
 *
 * @param args An array of arguments where args[0] is "cat" and subsequent args
 * are filenames.
 * @return 0 on success, or -1 on error.
 */
int s_cat(char** args);

/**
 * @brief Changes the permissions of a file.
 *
 * Updates the attribute byte of the specified file.
 *
 * @param fname The name of the file.
 * @param mode The new permission mode (e.g., to set/unset read-only or hidden
 * attributes).
 * @return 0 on success, or -1 on error.
 */
int s_chmod(const char* fname, int mode);

#endif
