#ifndef P_ERRNO_H
#define P_ERRNO_H

/** Global errno indicator */
extern int P_ERRNO;

/**
 * All PennOS error codes.
 * Ideally, every system call should map to at least one dedicated error code.
 * Extend this list when new errno is needed.
 */
typedef enum {
  P_NO_ERR = 0,  // No error

  /* Generic errors */
  P_EPERM,   // Operation not permitted
  P_EINVAL,  // Invalid argument
  P_ENOMEM,  // Out of memory

  /* Process-related errors */
  P_EPID,     // Process does not exist
  P_ECHILD,   // No child process available to wait on
  P_ESRCH,    // No such process
  P_ETHREAD,  // Thread creation failed

  /* File system–related errors */
  P_ENOENT,  // No such file or directory
  P_EEXIST,  // File already exists
  P_EISDIR,  // Is a directory (when it should not)
  P_EBADF,   // Invalid file descriptor
  P_EIO,     // I/O error
  P_ENOSPC,  // No space left on device
  P_EROFS,   // Read-only file system
  P_ENODEV,  // No such device (FS_NOT_MOUNTED)
  P_ENFILE,  // File table overflow (FS_GDT_FULL)
  P_EBUSY,   // Device or resource busy (FS_FILE_IN_USE)
  P_EACCES,  // Permission denied (FS_NO_PERMISSION)
  P_EMFILE,  // Too many open files (Process limit)

  /* Signal errors */
  P_SIGINT,   // failed to set SIGINT handler
  P_SIGTSTP,  // failed to set SIGTSTP handler

  /* Other errors */
  P_ENAMETOOLONG,  // File name too long
  P_E2BIG,         // Argument list too long

  /* TODO: You can extend this section with future errno. */

  P_ERR_MAX  // Sentinel: number of error codes (must not be used as a real
             // error)
} p_errno_t;

/**
 * @brief Print a PennOS-specific error message to stderr.
 *
 * Looks up the string for the current P_ERRNO value and prints it. If
 * @p msg is non-NULL and non-empty, it is used as a prefix:
 * "msg: error\n"; otherwise only the error message is printed.
 *
 * @param msg Optional prefix string.
 */
void u_perror(const char* msg);

/**
 * @brief Print a PennOS-specific error message to stderr.
 *
 * Same as u_perror but for PennFAT (using write instead of s_write).
 *
 * @param msg Optional prefix string.
 */
void f_perror(const char* msg);

#endif
