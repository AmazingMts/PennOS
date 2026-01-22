#include "p_errno.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../fat_syscalls.h"
#include "../fat_kernel.h"

int P_ERRNO = P_NO_ERR;
static char buf[256];

/* Lookup table mapping error codes to human-readable strings */
static const char* const p_errstr[] = {
    [P_NO_ERR] = "no error",

    /* Generic errors */
    [P_EPERM] = "operation not permitted",
    [P_EINVAL] = "invalid argument",
    [P_ENOMEM] = "malloc failure",

    /* Process-related errors */
    [P_EPID] = "no such process",
    [P_ECHILD] = "no child processes",
    [P_ESRCH] = "no such process",
    [P_ETHREAD] = "thread creation failed",

    /* File system–related errors */
    [P_ENOENT] = "no such file or directory",
    [P_EEXIST] = "file already exists",
    [P_EISDIR] = "not a regular file",
    [P_EBADF] = "bad file descriptor",
    [P_EIO] = "I/O error",
    [P_ENOSPC] = "no space left on disk",
    [P_EROFS] = "file is read-only",
    [P_ENODEV] = "filesystem not mounted",
    [P_ENFILE] = "open file table is full",
    [P_EBUSY] = "file is in use",
    [P_EACCES] = "permission denied",
    [P_EMFILE] = "too many open files",

    /* Signal errors */
    [P_SIGINT] = "failed to set SIGINT handler",
    [P_SIGTSTP] = "failed to set SIGTSTP handler",

    [P_ENAMETOOLONG] = "file name too long",
    [P_E2BIG] = "argument list too long",

    // TODO: When adding new error codes to p_errno_t,
    // remember to also add their string representations here.
};

void u_perror(const char* msg) {
  const char* err = "unknown PennOS error";

  if (P_ERRNO >= 0 && P_ERRNO < P_ERR_MAX) {
    err = p_errstr[P_ERRNO];
  }

  if (msg != NULL && msg[0] != '\0')
    snprintf(buf, sizeof(buf), "%s: %s\n", msg, err);
  else
    snprintf(buf, sizeof(buf), "%s\n", err);

  // fprintf(stderr, "%s", buf);
  s_write(STDERR_FILENO, buf, strlen(buf));
}

void f_perror(const char* msg) {
  const char* err = "unknown PennOS error";

  if (P_ERRNO >= 0 && P_ERRNO < P_ERR_MAX) {
    err = p_errstr[P_ERRNO];
  }

  if (msg != NULL && msg[0] != '\0')
    snprintf(buf, sizeof(buf), "%s: %s\n", msg, err);
  else
    snprintf(buf, sizeof(buf), "%s\n", err);

  k_write(STDERR_FILENO, buf, strlen(buf));
}
