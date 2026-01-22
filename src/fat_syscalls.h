#ifndef FAT_SYSCALLS_H_
#define FAT_SYSCALLS_H_

#include <stddef.h>
#include <sys/types.h>
#include "./util/struct.h"

#define F_READ 0x01
#define F_WRITE 0x02
#define F_APPEND 0x04

#define F_SEEK_SET 0
#define F_SEEK_CUR 1
#define F_SEEK_END 2

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/**
 * @brief Opens a file for reading, writing, or appending.
 *
 * Finds an available local file descriptor in the current process's file
 * descriptor table, calls k_open to open the file in the FAT file system, and
 * maps the local FD to the kernel FD.
 *
 * @param fname The name of the file to open.
 * @param mode The mode to open the file in (F_READ, F_WRITE, F_APPEND).
 * @return The local file descriptor (>= 0) on success, or -1 on failure.
 */
int s_open(const char* fname, int mode);

/**
 * @brief Reads data from an open file.
 *
 * Reads up to n bytes from the file associated with the local file descriptor
 * fd into the buffer buf.
 *
 * @param fd The local file descriptor.
 * @param n The maximum number of bytes to read.
 * @param buf The buffer to store the read data.
 * @return The number of bytes read, 0 on EOF, or -1 on error.
 */
ssize_t s_read(int fd, int n, char* buf);

/**
 * @brief Writes data to an open file.
 *
 * Writes n bytes from the string str to the file associated with the local file
 * descriptor fd.
 *
 * @param fd The local file descriptor.
 * @param str The buffer containing the data to write.
 * @param n The number of bytes to write.
 * @return The number of bytes written, or -1 on error.
 */
ssize_t s_write(int fd, const char* str, int n);

/**
 * @brief Closes an open file.
 *
 * Closes the file associated with the local file descriptor fd and releases
 * the corresponding kernel file descriptor.
 *
 * @param fd The local file descriptor to close.
 * @return 0 on success, or -1 on error.
 */
int s_close(int fd);

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
 * @brief Repositions the read/write file offset.
 *
 * Sets the file offset for the open file associated with fd.
 *
 * @param fd The local file descriptor.
 * @param offset The new offset relative to whence.
 * @param whence F_SEEK_SET (beginning), F_SEEK_CUR (current), or F_SEEK_END
 * (end).
 * @return The new offset from the beginning of the file, or -1 on error.
 */
off_t s_lseek(int fd, int offset, int whence);

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

/**
 * @brief Checks if the file has execute permissions.
 *
 * @param fname The name of the file.
 * @return 0 on success, or -1 on error.
 */
int s_check_executable(const char* fname);

#endif
