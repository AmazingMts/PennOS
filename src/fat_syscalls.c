#include "fat_syscalls.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "fat_kernel.h"
#include "process.h"
#include "util/struct.h"

/**
 * @brief Finds the next available local FD index in the PCB's fd_table.
 * @return The free local FD index (>= 3), or -1 if the table is full.
 */
static int s_find_local_fd_spot(void) {
  pcb_t* current_proc = get_current_process();
  if (!current_proc)
    return -1;
  for (int i = 3; i < MAX_FD; i++) {
    if (current_proc->fd_table[i] == -1) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Opens a file, delegating to k_open and linking the result to the PCB.
 */
int s_open(const char* fname, int mode) {
  int fd = s_find_local_fd_spot();
  if (fd == -1) {
    P_ERRNO = P_EMFILE;
    return -1;
  }

  int kfd_status = k_open(fname, mode);

  if (kfd_status < 0) {
    return -1;  // k_open failed and set P_ERRNO
  }
  int kfd = kfd_status;

  pcb_t* current_proc = get_current_process();
  if (current_proc) {
    current_proc->fd_table[fd] = kfd;
  } else {
    // close the file descriptor if no current_proc found.
    k_close(kfd);
    P_ERRNO = P_EPID;
    return -1;
  }

  return fd;
}

/**
 * @brief Reads data from the file, updating the process-local offset.
 */
ssize_t s_read(int fd, int n, char* buf) {
  pcb_t* current_proc = get_current_process();
  if (fd < 0 || fd >= MAX_FD || current_proc == NULL ||
      current_proc->fd_table[fd] == -1) {
    P_ERRNO = FS_BAD_FD;
    return -1;
  }
  int kfd = current_proc->fd_table[fd];

  ssize_t bytes_read = k_read(kfd, n, buf);

  return bytes_read;
}

/**
 * @brief Writes data to the file, updating the process-local offset and file
 * size.
 */
ssize_t s_write(int fd, const char* str, int n) {
  pcb_t* current_proc = get_current_process();
  if (fd < 0 || fd >= MAX_FD || current_proc == NULL ||
      current_proc->fd_table[fd] == -1) {
    P_ERRNO = FS_BAD_FD;
    return -1;
  }
  int kfd = current_proc->fd_table[fd];

  ssize_t bytes_written = k_write(kfd, str, n);

  return bytes_written;
}

/**
 * @brief Closes the file, cleaning up the local FD table and the unique
 * structure.
 */
int s_close(int fd) {
  pcb_t* current_proc = get_current_process();
  if (fd < 0 || fd >= MAX_FD || current_proc == NULL ||
      current_proc->fd_table[fd] == -1) {
    P_ERRNO = FS_BAD_FD;
    return -1;
  }
  int kfd = current_proc->fd_table[fd];

  int status = k_close(kfd);

  if (status >= 0) {  // k_close succeeds (returns 0 or positive status)
    current_proc->fd_table[fd] = -1;
  }

  return status;
}

/**
 * @brief Repositions the file pointer for a process's open file.
 */
off_t s_lseek(int fd, int offset, int whence) {
  pcb_t* current_proc = get_current_process();
  if (fd < 0 || fd >= MAX_FD || current_proc == NULL ||
      current_proc->fd_table[fd] == -1) {
    P_ERRNO = FS_BAD_FD;
    return -1;
  }
  int kfd = current_proc->fd_table[fd];

  off_t new_offset = k_lseek(kfd, offset, whence);

  return new_offset;
}

/**
 * @brief Unlinks (removes) a file from the file system.
 */
int s_unlink(const char* fname) {
  return k_unlink(fname);
}

// Helper function to format and write a directory entry
static void s_ls_callback(const dir_entry_t* entry) {
  char line[256];
  k_format_dirent(entry, line, sizeof(line));
  size_t len = strlen(line);
  if (len > 0) {
    s_write(STDOUT_FILENO, line, len);
  }
}

/**
 * @brief Lists files in the root directory.
 */
int s_ls(const char* filename) {
  // Use k_scan_dir to avoid duplicating FAT traversal logic.
  return k_scan_dir(filename, s_ls_callback);
}

int s_mv(const char* src, const char* dest) {
  return k_mv(src, dest);
}

int s_cp(char** args) {
  return k_cp(args);
}

int s_cat(char** args) {
  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;

  // Case 1: No arguments provided -> Read from STDIN
  if (args[1] == NULL) {
    while ((bytes_read = s_read(STDIN_FILENO, BUFFER_SIZE, buffer)) > 0) {
      if (s_write(STDOUT_FILENO, buffer, bytes_read) != bytes_read) {
        P_ERRNO = FS_IO_ERROR;
        return -1;
      }
    }
    if (bytes_read < 0) {
      return -1;
    }
    return 0;
  }

  // Case 2: Arguments provided -> Read from each file
  int status = 0;
  for (int i = 1; args[i] != NULL; i++) {
    int fd = s_open(args[i], F_READ);
    if (fd < 0) {
      status = -1;
      continue;
    }

    while ((bytes_read = s_read(fd, BUFFER_SIZE, buffer)) > 0) {
      if (s_write(STDOUT_FILENO, buffer, bytes_read) != bytes_read) {
        P_ERRNO = FS_IO_ERROR;
        s_close(fd);
        return -1;
      }
    }

    s_close(fd);

    if (bytes_read < 0) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "cat: Error reading %s\n",
               args[i]);
      s_write(STDERR_FILENO, error_msg, strlen(error_msg));
      status = -1;
    }
  }

  return status;
}

int s_chmod(const char* fname, int mode) {
  return k_chmod_update(fname, (uint8_t)mode);
}

int s_check_executable(const char* fname) {
  return k_check_executable(fname);
}
