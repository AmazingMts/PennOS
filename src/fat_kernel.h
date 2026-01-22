#ifndef FAT_KERNEL_H
#define FAT_KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "util/p_errno.h"
#include "util/struct.h"

#define FS_SUCCESS P_NO_ERR

// FS error codes mapped to P_ERRNO codes
#define FS_NOT_MOUNTED P_ENODEV
#define FS_GDT_FULL P_ENFILE
#define FS_DISK_FULL P_ENOSPC

#define FS_FILE_NOT_FOUND P_ENOENT
#define FS_FILE_IN_USE P_EBUSY
#define FS_NOT_A_FILE P_EISDIR

#define FS_NO_PERMISSION P_EACCES
#define FS_READ_ONLY_MODE P_EROFS
#define FS_INVALID_MODE P_EINVAL
#define FS_INVALID_OFFSET P_EINVAL
#define FS_INVALID_WHENCE P_EINVAL
#define FS_BAD_FD P_EBADF

#define FS_IO_ERROR P_EIO
#define FS_MALLOC_FAIL P_ENOMEM
#define FS_INVALID_ARG P_EINVAL

#define MAX_GDT_ENTRY 1024
#define BUFFER_SIZE 4096

// permission flags
#define F_READ 0x01
#define F_WRITE 0x02
#define F_APPEND 0x04

// whence options
#define F_SEEK_SET 0
#define F_SEEK_CUR 1
#define F_SEEK_END 2

static const size_t BLOCK_SIZE_MAP[] = {256, 512, 1024, 2048, 4096};

// 1. FAT Pointer (memory-mapped array of 2-byte entries)
extern uint16_t* FAT_TABLE;

// 2. Metadata determined during mkfs
extern int FS_HOST_FD;
extern size_t FS_BLOCK_SIZE;
extern size_t FS_FAT_SIZE;
extern size_t FS_NUM_ENTRIES;
extern size_t FS_FAT_BLOCKS;

extern open_file_t* GLOBAL_FD_TABLE[MAX_GDT_ENTRY];

extern bool IS_FS_MOUNTED;

typedef struct {
  char name[32];
  uint32_t size;
  uint16_t firstBlock;
  uint8_t type;  //(1: Regular file, 2: Directory file).
  uint8_t perm;  // refer to specification
  time_t mtime;  // last modification time
  char reserved[16];
} dir_entry_t;

/**
 * @brief Create and initialize a new PennFAT filesystem.
 *
 * Creates a new filesystem file named @p fs_name and initializes it with a
 * PennFAT layout. The FAT region occupies @p blocks_in_fat blocks, each of
 * size determined by @p block_size_config. The function:
 *   - Initializes the FAT, storing configuration in FAT[0].
 *   - Marks block 1 as the root directory, all remaining data blocks as free.
 *   - Zero-fills the entire data region on disk.
 *
 * This function must be called before mounting and using the filesystem.
 *
 * @param fs_name           Path/name of the filesystem image to create.
 * @param blocks_in_fat     Number of blocks to allocate for the FAT (1–32).
 * @param block_size_config Index into BLOCK_SIZE_MAP selecting the block size.
 *
 * @return FS_SUCCESS on success, or -1 on invalid configuration or I/O errors.
 */
int mkfs(const char* fs_name, int blocks_in_fat, int block_size_config);

/**
 * @brief Mount an existing PennFAT filesystem image.
 *
 * Opens the backing file @p fs_name and initializes global filesystem metadata
 * from the FAT header stored in FAT[0]. On success, the filesystem is ready for
 * directory and file operations.
 *
 * @param fs_name Path/name of the existing PennFAT filesystem image to mount.
 *
 * @return FS_SUCCESS on success; -1 on configuration, I/O, or mmap errors.
 */
int mount(const char* fs_name);

/**
 * @brief Unmount the currently mounted PennFAT filesystem.
 *
 * Cleans up all global resources associated with the mounted filesystem.
 * Specifically, this function:
 *   - Verifies that a filesystem is currently mounted.
 *   - Cleans up the global descriptor table via k_gdt_cleanup().
 *   - Unmaps the FAT region from memory (FAT_TABLE).
 *   - Closes the backing filesystem file (FS_HOST_FD).
 *   - Clears the IS_FS_MOUNTED flag on success.
 *
 * If any errors occur during unmapping or closing, they are reported to
 * stderr, and the function returns an error code while noting that the
 * unmount completed with errors.
 *
 * @return FS_SUCCESS on full success; FS_NOT_MOUNTED if no filesystem
 *         was mounted; or -1 if unmapping or closing the backing file fails.
 */
int unmount(void);

/**
 * @brief Search the root directory for a file with the given name.
 *
 * The @p offset out-parameter is set as follows:
 *  - If the function returns @c true:  byte offset of the existing matching
 *    directory entry within the filesystem image.
 *  - If the function returns @c false and a reusable/free slot exists
 *    (deleted entry or first end-of-directory entry): byte offset of the
 *    first such slot where a new entry can be created.
 *  - If the function returns @c false and no free slot exists in any root
 *    directory block: set to -1.
 *
 * @param [in]  fname  filename to search for in the root directory.
 * @param [out] offset Output pointer that receives the byte offset of either
 * the found entry or the first suitable free slot (or -1 if none).
 *
 * @return @c true if a directory entry with name @p fname exists,
 *         @c false otherwise.
 */
bool k_find_file(const char* fname, off_t* offset);

/**
 * @brief Open (or create) a file and allocate a global file descriptor.
 *
 * This is the main entry point for opening a file in the PennFAT filesystem.
 * It:
 *   - Verifies that a filesystem is mounted and that @p mode is valid.
 *   - Finds a free slot in the global descriptor table (GDT).
 *   - Looks up @p fname in the root directory via k_find_file().
 *   - If not found and the directory is full, attempts to extend the root
 *     directory via k_extend_root().
 *   - Prevents multiple writers by checking existing open files when
 *     opening in F_WRITE or F_APPEND (k_have_write_opened()).
 *   - Dispatches to the appropriate helper:
 *       - k_open_mode_read()   for F_READ
 *       - k_open_mode_write()  for F_WRITE (create/truncate)
 *       - k_open_mode_append() for F_APPEND (create/append)
 *   - On success, installs the new open_file_t into GLOBAL_FD_TABLE.
 *
 * @param fname Name of the file to open (null-terminated string).
 * @param mode  Open mode: F_READ, F_WRITE, or F_APPEND.
 *
 * @retval non-negative file descriptor index on success;
 * @retval FS_NOT_MOUNTED   if no filesystem is mounted
 * @retval FS_INVALID_MODE  if @p mode is not supported
 * @retval FS_GDT_FULL      if the global descriptor table is full
 * @retval FS_DISK_FULL     if the root cannot be extended
 * @retval FS_FILE_IN_USE   if multiple writers are found
 */
int k_open(const char* fname, int mode);

/**
 * @brief Read data from an open file into a buffer.
 *
 * This function reads up to @p n bytes from the file associated with
 * the global descriptor @p fd into @p buf, starting at the file's
 * current offset. The file offset is advanced by the number of bytes
 * actually read.
 *
 * Special cases and behavior:
 *   - If @p fd is 0, the read is delegated directly to the host
 *     standard input via read(0, buf, n).
 *   - The file must have read permission; otherwise, FS_NO_PERMISSION is
 *     returned.
 *   - At most @p n bytes are read, but fewer bytes may be returned:
 *       - if the read would go past the end of the file (EOF),
 *         it is truncated to the remaining bytes in the file;
 *       - if fewer bytes are available in the current block;
 *       - or if an underlying pread() returns fewer bytes.
 *   - If the current file offset is beyond EOF, 0 is returned immediately.
 *   - If the FAT chain ends before reaching the requested offset,
 *     FS_INVALID_OFFSET is returned.
 *
 * @param fd  Global file descriptor index (0 for STDIN, otherwise an index
 *            into GLOBAL_FD_TABLE).
 * @param n   Maximum number of bytes to read.
 * @param buf Buffer to receive the data; must have space for at least @p n
 * bytes.
 *
 * @return On success, the number of bytes actually read (0 on EOF);
 *         FS_NO_PERMISSION or FS_INVALID_OFFSET on logical errors;
 *         or the value returned by pread() (<= 0) if an underlying I/O
 *         error or EOF occurs before any data is read.
 */
ssize_t k_read(int fd, int n, char* buf);

/**
 * @brief Write data to an open file or standard output/error.
 *
 * Writes up to @p n bytes from @p str to the file associated with the
 * global descriptor @p fd, starting at the file's current offset. The
 * offset is advanced by the number of bytes actually written, and the
 * file size is extended if necessary.
 *
 * Special handling:
 *   - If @p fd is 1 or 2, the write is delegated directly to the host
 *     STDOUT or STDERR via write(fd, str, n).
 *   - If the disk becomes full, the function stops early and returns the
 *     number of bytes successfully written so far.
 *
 * @param fd   Global file descriptor index (1 for STDOUT, 2 for STDERR,
 *             otherwise an index into GLOBAL_FD_TABLE).
 * @param str  Buffer containing the data to be written.
 * @param n    Maximum number of bytes to write from @p str.
 *
 * @retval >=0               Number of bytes actually written (may be less
 *                           than @p n, including 0 if nothing was written).
 * @retval FS_NO_PERMISSION  File is not open with F_WRITE or F_APPEND.
 * @retval FS_INVALID_OFFSET Current file offset does not correspond to a valid
 *                           FAT chain position.
 */
ssize_t k_write(int fd, const char* str, int n);

/**
 * @brief Close a kernel-level file descriptor.
 *
 * Behavior:
 * - Validates that the filesystem is mounted and that @p kfd refers to an
 *   open entry in the global file descriptor table.
 * - For standard descriptors (0, 1, 2), only the in-memory open_file_t is
 *   freed; no directory entry is read or written.
 * - For other descriptors, the directory entry is read, and if the file was
 *   opened with write/append flags, its size and modification time are
 *   updated.
 * - If the file has been previously unlinked (directory entry name[0] == 2),
 *   this function checks whether this is the last open descriptor referencing
 *   it. If so, it frees the file's FAT chain and marks the directory entry as
 *   truly deleted (name[0] == 1).
 *
 * @param kfd Kernel file descriptor to close (0..MAX_FD-1).
 *
 * @retval FS_SUCCESS      The file descriptor was successfully closed.
 * @retval FS_NOT_MOUNTED  The filesystem is not mounted.
 * @retval FS_BAD_FD       @p kfd is out of range or not associated with an
 *                         open file.
 * @retval FS_IO_ERROR     An unexpected I/O error occurred while reading or
 *                         writing the directory entry.
 */
int k_close(int kfd);

/**
 * @brief Unlink (delete) a file from the filesystem namespace.
 *
 * Removes the directory entry for the given file name. If the file is still
 * referenced by one or more open file descriptors, the directory entry is
 * marked as "deleted but still in use" (name[0] == 2) so that the actual data
 * blocks are freed only when the last descriptor is closed. If no descriptors
 * reference the file, its FAT chain is freed immediately and the directory
 * entry is marked as deleted (name[0] == 1).
 *
 * @param fname Null-terminated path of the file to unlink, relative to the
 *              current directory / filesystem root (implementation-defined).
 *
 * @retval FS_SUCCESS        The file was successfully unlinked (either marked
 *                           as deleted or deleted immediately).
 * @retval FS_NOT_MOUNTED    The filesystem is not mounted.
 * @retval FS_FILE_NOT_FOUND No directory entry matching @p fname was found.
 * @retval FS_NOT_A_FILE     The found entry is a directory, not a regular file.
 * @retval FS_IO_ERROR       An I/O error occurred while reading or writing the
 *                           directory entry.
 */
int k_unlink(const char* fname);

/**
 * @brief Reposition the file offset for an open file descriptor.
 *
 * This function changes the current read/write offset of the file associated
 * with the kernel file descriptor @p kfd, the new position is computed
 * according to @p offset and @p whence.
 *
 * If the resulting position is greater than the current in-memory file size and
 * the file is opened with F_WRITE or F_APPEND, this function updates the
 * in-memory size (open file table entry) to @p new_pos.
 *
 * @param kfd    Kernel file descriptor index in GLOBAL_FD_TABLE.
 * @param offset Offset value interpreted according to @p whence.
 * @param whence One of F_SEEK_SET, F_SEEK_CUR, or F_SEEK_END.
 *
 * @retval FS_SUCCESS          The offset was successfully updated.
 * @retval FS_NOT_MOUNTED      The filesystem is not mounted.
 * @retval FS_BAD_FD           @p kfd is out of range or not associated with an
 *                             open file.
 * @retval FS_INVALID_WHENCE   @p whence has an invalid value.
 * @retval FS_INVALID_OFFSET   The computed new position is negative.
 */
int k_lseek(int kfd, int offset, int whence);

/**
 * @brief List directory contents or show information about a single file.
 *
 * If a @p filename is given, prints the specified file in the current directory
 * (root). If the file does not exist, an error code is returned. If it's NULL,
 * prints all valid directory entries in the root directory.
 *
 * @param filename  Optional name of the file to display. If NULL, all files in
 *                  the root directory are listed.
 *
 * @retval FS_SUCCESS        on success;
 * @retval FS_NOT_MOUNTED    if the filesystem is not mounted;
 * @retval FS_FILE_NOT_FOUND if the specified file cannot be found;
 * @retval FS_IO_ERROR       if a read error occurs while accessing the
 *                           underlying host file descriptor.
 */
int k_ls(const char* filename);

/**
 * @brief Format a directory entry into a human-readable string.
 *
 * Formats the directory entry similar to `ls -l`.
 *
 * @param entry Pointer to the directory entry to format.
 * @param buffer Buffer to store the formatted string.
 * @param size Size of the buffer.
 */
void k_format_dirent(const dir_entry_t* entry, char* buffer, size_t size);

/**
 * @brief Iterate over directory entries.
 *
 * If filename is provided, invokes callback for that specific file.
 * If filename is NULL, invokes callback for all files in the root directory.
 *
 * @param filename Optional filename to search for.
 * @param callback Function to call for each found entry.
 * @return FS_SUCCESS on success, or error code.
 */
int k_scan_dir(const char* filename, void (*callback)(const dir_entry_t*));

/**
 * @brief Implementation of the PennFAT cat command.
 *
 * Supports the following forms:
 *   - cat                    : copy STDIN to STDOUT
 *   - cat FILE1 [FILE2 ...]  : print one or more files to STDOUT
 *   - cat FILE1 [...] -w OUT : write concatenated input to OUT
 *                              (truncate/create)
 *   - cat FILE1 [...] -a OUT : append concatenated input to OUT (create if
 *                              needed)
 *
 * @param args NULL-terminated argument vector, where args[0] is "cat" and
 *             subsequent elements are filenames and/or options (-w/-a).
 *
 * @retval FS_SUCCESS        All requested input was processed without fatal
 * errors.
 * @retval FS_NOT_MOUNTED    No filesystem is currently mounted.
 * @retval FS_FILE_NOT_FOUND Missing output filename after -w or -a.
 * @retval FS_IO_ERROR       Failed to open/create the output file, or a stream
 *                           copy operation failed.
 * @retval Other negative FS_* error codes propagated from k_open() / k_close()
 *         on input or output files (e.g., FS_FILE_NOT_FOUND, FS_NO_PERMISSION).
 */
int k_cat(char** args);

/**
 * @brief Update the permission bits of a file.
 *
 * @param fname    Name of the file whose permissions should be changed.
 * @param new_perm New permission bits (bitmask of r/w/x flags).
 *
 * @retval FS_SUCCESS        Permission update succeeded.
 * @retval FS_NOT_MOUNTED    No filesystem is currently mounted.
 * @retval FS_FILE_NOT_FOUND The specified file does not exist in the directory.
 * @retval FS_IO_ERROR       An I/O error occurred while reading or writing
 *                           the directory entry to disk.
 */
int k_chmod_update(const char* fname, uint8_t new_perm);

/**
 * @brief Checks if the file has execute permissions.
 *
 * @param fname The name of the file to check.
 * @return FS_SUCCESS if executable, or -1 (FS_NO_PERMISSION/other errors).
 */
 int k_check_executable(const char* fname);

/**
 * @brief Rename (move) a file within the PennFAT filesystem.
 *
 * Renames the directory entry for @p source to @p dest within the same
 * directory (no data blocks are moved or copied). If @p dest already
 * exists, it is first unlinked (subject to its permissions) and then
 * @p source is renamed to @p dest.
 *
 * @param source Current name of the file to be renamed.
 * @param dest   New name for the file.
 *
 * @retval FS_SUCCESS        The file was successfully renamed.
 * @retval FS_NOT_MOUNTED    No filesystem is currently mounted.
 * @retval FS_FILE_NOT_FOUND The source file does not exist.
 * @retval FS_NO_PERMISSION  The source is not readable, or the existing
 *                           destination is not writable.
 * @retval FS_IO_ERROR       An I/O error occurred while reading or writing
 *                           directory entries to disk.
 * @retval Other negative FS_* error codes propagated from k_unlink() when
 *         removing an existing destination.
 */
int k_mv(const char* source, const char* dest);

/**
 * @brief Copy files between host and PennFAT, or within PennFAT.
 *
 * Supported modes:
 *   - PennFAT -> PennFAT (P2P):   cp SOURCE DEST
 *   - Host    -> PennFAT (H2P):   cp -h SOURCE DEST
 *   - PennFAT -> Host (P2H)   :   cp SOURCE -h DEST
 *
 * @param args NULL-terminated argument vector; args[0] is "cp", and the
 *             remaining arguments specify mode and paths as described above.
 *
 * @retval FS_SUCCESS        Copy completed successfully.
 * @retval FS_NOT_MOUNTED    No PennFAT filesystem is currently mounted.
 * @retval FS_INVALID_ARG    Invalid or insufficient arguments (e.g. missing
 *                           source or destination path).
 * @retval FS_IO_ERROR       A stream copy failed, or an internal host/PennFAT
 *                           transfer helper returned an error.
 * @retval Other negative FS_* error codes propagated from k_open() / k_close()
 *         (e.g., FS_FILE_NOT_FOUND, FS_NO_PERMISSION) or from the helper
 *         functions used for host/PennFAT transfers.
 */
int k_cp(char** args);

#endif