#include "fat_kernel.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "./util/parser.h"

/** @brief FAT table in memory */
uint16_t* FAT_TABLE = NULL;

/** @brief Global open file table */
open_file_t* GLOBAL_FD_TABLE[MAX_GDT_ENTRY] = {0};

/** @brief fd to the filesystem file on host OS */
int FS_HOST_FD = -1;

/** @brief config: block size of current fs */
size_t FS_BLOCK_SIZE = 0;

/** @brief config: FAT table size of current fs */
size_t FS_FAT_SIZE = 0;

/** @brief config: number of FAT entries of current fs */
size_t FS_NUM_ENTRIES = 0;

/** @brief config: number of FAT blocks of current fs */
size_t FS_FAT_BLOCKS = 0;

/** @brief config: entries per block of current fs */
size_t FS_ENTRY_PER_BLK = 0;

/** @brief whether a filesystem is mounted at the mooment */
bool IS_FS_MOUNTED = false;

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Declarations =================== /////
////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initialize the global file descriptor table for standard streams.
 *
 * Allocates and initializes entries 0, 1, and 2 in GLOBAL_FD_TABLE to represent
 * STDIN, STDOUT, and STDERR with appropriate access flags. Must be called
 * before any code relies on the global file descriptor table.
 */
static void k_gdt_init(void);

/**
 * @brief Clean up the global file descriptor table.
 *
 * Frees all non-NULL entries in GLOBAL_FD_TABLE and resets their pointers to
 * NULL. Intended to be called when shutting down or unmounting in order to
 * release all resources associated with global file descriptors.
 */
static void k_gdt_cleanup(void);

/**
 * @brief Find a free slot in the global descriptor table (GDT).
 *
 * @return Index of the free GDT slot, or -1 if no free slot is available.
 */
static int k_find_gdt_spot(void);

/**
 * @brief Find the first free data block in the FAT.
 *
 * Scans FAT_TABLE from entry 1 (block 0 is reserved) for an entry with
 * value 0x0000, indicating a free block.
 *
 * @return Index of the first free block, or 0 if no free block is available.
 */
static int k_find_free_block(void);

/**
 * @brief Append a new data block to the root directory.
 *
 * This function updates the FAT and zeros the contents of the newly allocated
 * block in the data region.
 *
 * @return On success, the byte offset within the filesystem image of the
 *         start of the newly allocated root-directory block.
 *         On failure (no free blocks available), returns -1.
 */
static off_t k_extend_root(void);

/**
 * @brief this function frees up a given block chain on the FAT.
 *
 * @param first_block the index of the head of the chain.
 */
static void k_free_fat_chain(uint16_t first_block);

/**
 * @brief Checks if a file is already open in an exclusive (F_WRITE or F_APPEND)
 * mode.
 * * @param fname The name of the file to check.
 * @return whether an write instance is opened
 */
static bool k_have_write_opened(const char* fname);

/**
 * @brief caller uses this function to check whether a dirent is still
 * referenced by any open file entry other than the param dirent.
 *
 * @param dirent_offset the offset used to identify the dirent
 * @return true if the dirent IS still referenced, false otherwise.
 *
 * @note k_close (and other caller) should remove candidate from open file table
 * first so that this function wouldn't cound itself as a source of reference.
 */
static bool k_is_file_still_open(off_t dirent_offset);

/**
 * @brief Updates the directory entry on disk with the current metadata from the
 * open_file_t structure (firstBlock, size, mtime).
 *
 * @param file_data The open_file_t structure containing the new metadata.
 * @return FS_SUCCESS (0) on success, negative value on error.
 */
static int k_update_dirent(open_file_t* file_data);

/**
 * @brief Print a directory entry in a human-readable format.
 *
 * Formats and prints a single directory entry similar to `ls -l`, including
 * block index, type/permission bits, size, last modification time, and name.
 *
 * @param entry Pointer to the directory entry to print (must not be NULL).
 */
static void k_print_dirent(const dir_entry_t* entry);

/**
 * @brief Helper to open a file in read-only mode.
 *
 * Validates that the directory entry exists and refers to a regular file,
 * then allocates and initializes an open_file_t structure for read access.
 * The directory entry is read from disk at @p offset to populate metadata.
 *
 * On success, @p *new_of_out will point to a newly allocated open_file_t,
 * with its name, size, permissions, first block, and dirent offset copied
 * from @p entry. The reference count is initialized to 1 and the flag is
 * set to F_READ.
 *
 * @param fname      Name of the file being opened (null-terminated string).
 * @param offset     Byte offset of the corresponding dir_entry_t on disk.
 * @param new_of_out Output parameter; on success, receives a newly allocated
 *                   open_file_t pointer.
 * @param found      Indicates whether the directory entry was already found
 *                   in the directory scan.
 * @param entry      Pointer to a dir_entry_t buffer used to read the entry
 *                   from disk and to provide metadata.
 *
 * @return FS_SUCCESS on success; an FS_* error code such as FS_FILE_NOT_FOUND,
 *         FS_NOT_A_FILE, or FS_MALLOC_FAIL on failure.
 */
static int k_open_mode_read(const char* fname,
                            off_t offset,
                            open_file_t** new_of_out,
                            bool found,
                            dir_entry_t* entry);

/**
 * @brief Helper to open a file in write (truncate/create) mode.
 *
 * If the directory entry does not exist, this function creates a new regular
 * file entry on disk at @p offset, with default permissions (read/write).
 * If the entry exists and is a regular file, it truncates the file: any
 * existing FAT chain is freed, size is reset to 0, and the directory entry
 * is updated on disk.
 *
 * In both cases, it allocates and initializes an open_file_t for write
 * access. The file offset is set to 0 and the flag is set to F_WRITE.
 *
 * @param fname      Name of the file being opened (null-terminated string).
 * @param offset     Byte offset of the corresponding dir_entry_t on disk
 *                   where the entry should exist or be created.
 * @param new_of_out Output parameter; on success, receives a newly allocated
 *                   open_file_t pointer.
 * @param found      Indicates whether the directory entry was already found
 *                   in the directory scan.
 * @param entry      Pointer to a dir_entry_t buffer used both for creating
 *                   a new entry in memory and for reading/updating an
 *                   existing entry from/to disk.
 *
 * @return FS_SUCCESS on success; an FS_* error code such as FS_NOT_A_FILE,
 *         FS_IO_ERROR, or FS_MALLOC_FAIL on failure.
 */
static int k_open_mode_write(const char* fname,
                             off_t offset,
                             open_file_t** new_of_out,
                             bool found,
                             dir_entry_t* entry);

/**
 * @brief Helper to open a file in append mode.
 *
 * If the directory entry does not exist, this function creates a new regular
 * file entry on disk at @p offset, with default read/write permissions.
 * If the entry exists, it validates that it is a regular file but does not
 * truncate it or modify its contents.
 *
 * It then allocates and initializes an open_file_t for append access. The
 * open file's offset is set to the current file size so that all future
 * writes will append to the end. The flag is set to F_APPEND.
 *
 * @param fname      Name of the file being opened (null-terminated string).
 * @param offset     Byte offset of the corresponding dir_entry_t on disk
 *                   where the entry should exist or be created.
 * @param new_of_out Output parameter; on success, receives a newly allocated
 *                   open_file_t pointer.
 * @param found      Indicates whether the directory entry was already found
 *                   in the directory scan.
 * @param entry      Pointer to a dir_entry_t buffer used both for creating
 *                   a new entry in memory and for reading an existing entry
 *                   from disk.
 *
 * @return FS_SUCCESS on success; an FS_* error code such as FS_NOT_A_FILE,
 *         FS_IO_ERROR, or FS_MALLOC_FAIL on failure.
 */
static int k_open_mode_append(const char* fname,
                              off_t offset,
                              open_file_t** new_of_out,
                              bool found,
                              dir_entry_t* entry);

/**
 * @brief Reads content from pennfat_fd (PennFAT FS) and writes it to host_path
 * (host FS) using KERNEL k_read and HOST write.
 *
 * @param pennfat_fd The source PennFAT file descriptor.
 * @param host_path  The destination host file path.
 * @return 0 on success, -1 on error.
 */
static int k_pennfat_read_to_host_write(int pennfat_fd, const char* host_path);

/**
 * @brief Reads content from host_path (host FS) and writes it to pennfat_fd
 * (PennFAT FS) using HOST read and KERNEL k_write.
 *
 * @param host_path  The source host file path.
 * @param pennfat_fd The destination PennFAT file descriptor.
 * @return 0 on success, -1 on error.
 */
static int k_host_read_to_pennfat_write(const char* host_path, int pennfat_fd);

/**
 * @brief Reads content from input_fd and writes it to output_fd using
 * k_read/k_write. This function performs the core stream copying logic for the
 * 'cat' and 'cp' commands.
 *
 * @param input_fd  The PennFAT file descriptor (0=STDIN, or opened file).
 * @param output_fd The PennFAT file descriptor (1=STDOUT, or opened file).
 * @return 0 on success, -1 on error.
 */
static int copy_stream_content(int input_fd, int output_fd);

//////////////////////////////////////////////////////////////////////////////
// =================== Public API Implementation =================== ////////
////////////////////////////////////////////////////////////////////////////

int mkfs(const char* fs_name, int blocks_in_fat, int block_size_config) {
  if (IS_FS_MOUNTED) {
    k_write(2, "unexpected command.\n", strlen("unexpected command.\n"));
    return -1;
  }

  if (blocks_in_fat < 1 || blocks_in_fat > 32 || block_size_config < 0 ||
      block_size_config >= 5) {
    k_write(2, "Error: Invalid mkfs configuration.\n",
            strlen("Error: Invalid mkfs configuration.\n"));
    return -1;
  }

  size_t block_size = BLOCK_SIZE_MAP[block_size_config];
  size_t fat_size = block_size * blocks_in_fat;
  size_t num_fat_entries = fat_size / sizeof(uint16_t);
  // Data region starts at Block 1
  if (num_fat_entries == 65536)
    num_fat_entries--;
  size_t data_region_size = block_size * (num_fat_entries - 1);
  size_t total_fs_size = fat_size + data_region_size;

  int fd = open(fs_name, O_CREAT | O_RDWR, 0666);
  if (fd == -1) {
    perror("Error creating file system file");
    return -1;
  }

  if (ftruncate(fd, total_fs_size) == -1) {
    perror("Error resizing file system file");
    close(fd);
    return -1;
  }

  uint16_t* temp_fat =
      mmap(NULL, fat_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (temp_fat == MAP_FAILED) {
    perror("Error mmaping FAT region for initialization");
    close(fd);
    return -1;
  }

  // FAT[0]: MSB=blocks_in_fat, LSB=block_size_config
  temp_fat[0] = (uint16_t)((blocks_in_fat << 8) | block_size_config);

  // Initialize FAT[1:] and data region
  char zero_buf[block_size];
  memset(zero_buf, 0, block_size);  // zero buffer to later cleanse data blocks.
  for (size_t i = 1; i < num_fat_entries; i++) {
    if (i == 1) {  // Block 1 is the Root Directory, marked as last block
      temp_fat[i] = 0xFFFF;
    } else {  // Mark all other blocks as free (0x0000)
      temp_fat[i] = 0x0000;
    }
    // initialize entire data region to be all zeros.
    if (pwrite(fd, zero_buf, block_size, fat_size + (i - 1) * block_size) !=
        block_size) {
      perror("Error cleansing initial fs");  // don't necessarily need to exit
    }
  }

  munmap(temp_fat, fat_size);
  close(fd);

  char buf[256];
  int len = snprintf(
      buf, sizeof(buf),
      "PennFAT filesystem '%s' created successfully (Size: %zu bytes).\n",
      fs_name, total_fs_size);
  k_write(1, buf, len);
  return FS_SUCCESS;
}

int mount(const char* fs_name) {
  if (IS_FS_MOUNTED) {
    k_write(2, "unexpected command.\n", strlen("unexpected command.\n"));
    return -1;
  }

  FS_HOST_FD = open(fs_name, O_RDWR);
  if (FS_HOST_FD == -1) {
    perror("Error opening file system file");
    return -1;
  }

  uint16_t fat0_data;
  if (pread(FS_HOST_FD, &fat0_data, sizeof(uint16_t), 0) != sizeof(uint16_t)) {
    k_write(2, "Error reading FAT header.\n",
            strlen("Error reading FAT header.\n"));
    close(FS_HOST_FD);
    FS_HOST_FD = -1;
    return -1;
  }

  FS_FAT_BLOCKS = (fat0_data >> 8) & 0xFF;   // MSB
  int block_size_config = fat0_data & 0xFF;  // LSB

  if (FS_FAT_BLOCKS < 1 || FS_FAT_BLOCKS > 32 || block_size_config < 0 ||
      block_size_config >= 5) {
    k_write(2, "Error: Invalid FAT configuration read from file.\n",
            strlen("Error: Invalid FAT configuration read from file.\n"));
    close(FS_HOST_FD);
    FS_HOST_FD = -1;
    return -1;
  }

  FS_BLOCK_SIZE = BLOCK_SIZE_MAP[block_size_config];
  FS_FAT_SIZE = FS_BLOCK_SIZE * FS_FAT_BLOCKS;
  FS_NUM_ENTRIES = FS_FAT_SIZE / sizeof(uint16_t);
  FS_ENTRY_PER_BLK = FS_BLOCK_SIZE / sizeof(dir_entry_t);

  FAT_TABLE = mmap(NULL, FS_FAT_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   FS_HOST_FD, 0);
  if (FAT_TABLE == MAP_FAILED) {
    perror("Error mmaping FAT region");
    close(FS_HOST_FD);
    FS_HOST_FD = -1;
    FAT_TABLE = NULL;
    return -1;
  }

  IS_FS_MOUNTED = true;
  k_gdt_init();

  char buf[256];
  int len =
      snprintf(buf, sizeof(buf),
               "PennFAT filesystem '%s' mounted successfully.\n", fs_name);
  k_write(1, buf, len);
  return FS_SUCCESS;
}

int unmount(void) {
  if (!IS_FS_MOUNTED) {
    k_write(2, "unexpected command.\n", strlen("unexpected command.\n"));
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  int result = FS_SUCCESS;

  k_gdt_cleanup();

  if (FAT_TABLE != NULL) {
    if (munmap(FAT_TABLE, FS_FAT_SIZE) == -1) {
      perror("Error unmapping FAT region");
      result = -1;
    }
  }

  if (FS_HOST_FD != -1) {
    if (close(FS_HOST_FD) == -1) {
      perror("Error closing file system file");
      result = -1;
    }
  }

  if (result == FS_SUCCESS) {
    IS_FS_MOUNTED = false;
    k_write(1, "PennFAT filesystem unmounted successfully.\n",
            strlen("PennFAT filesystem unmounted successfully.\n"));
  } else {
    k_write(2, "Unmount completed with errors.\n",
            strlen("Unmount completed with errors.\n"));
  }

  return result;
}

bool k_find_file(const char* fname, off_t* offset) {
  dir_entry_t entry;
  off_t first_free = -1;  // This is to mark the first seen free entry space.

  uint16_t blknum = 1;
  while (blknum != 0xFFFF) {  // traverse through root directory
    for (int i = 0; i < FS_ENTRY_PER_BLK; i++) {
      // calculate the offset of the current dirent and load the dirent
      off_t off =
          FS_FAT_SIZE + (blknum - 1) * FS_BLOCK_SIZE + i * sizeof(dir_entry_t);
      pread(FS_HOST_FD, &entry, sizeof(dir_entry_t), off);

      if (entry.name[0] == 0) {
        // If we reached the end of directory:
        //   - this means we didn't find the file.
        //   - if no previous free dirent found, set it to current offset and
        //   return false.

        // TODO not sure if initalized in mkfs
        if (first_free == -1)
          first_free = off;
        *offset = first_free;
        return false;
      }
      if (entry.name[0] == 1) {
        // If we found a deleated dirent:
        //   - if this is the first time, mark its location (might need later).
        if (first_free == -1)
          first_free = off;
        continue;
      }
      if (entry.name[0] == 2) {
        continue;  // File is technically "deleted" but actually still in use.
      }
      // If found: great
      if (strcmp(entry.name, fname) == 0) {
        *offset = off;
        return true;
      }
    }
    blknum = FAT_TABLE[blknum];
  }

  // getting here means we traversed the whole root directory yet didn't reach
  // the "end of directory". First free might be -1 or not. Return false.
  *offset = first_free;
  return false;
}

int k_open(const char* fname, int mode) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }
  if (mode != F_WRITE && mode != F_READ && mode != F_APPEND) {
    P_ERRNO = FS_INVALID_MODE;
    return -1;
  }

  int fd = k_find_gdt_spot();
  if (fd == -1) {
    P_ERRNO = FS_GDT_FULL;
    return -1;
  }

  off_t offset = 0;
  bool found = k_find_file(fname, &offset);

  // If file not found and directory is full, try to extend root.
  if (!found && offset == -1) {
    offset = k_extend_root();
    if (offset == -1) {
      P_ERRNO = FS_DISK_FULL;
      return -1;
    }
  }

  open_file_t* new_of = NULL;
  dir_entry_t entry;
  int result = FS_SUCCESS;

  // check for multiple write
  if (found && (mode == F_WRITE || mode == F_APPEND)) {
    if (k_have_write_opened(fname)) {
      P_ERRNO = FS_FILE_IN_USE;
      return -1;
    }
  }

  if (mode == F_READ) {
    result = k_open_mode_read(fname, offset, &new_of, found, &entry);
  } else if (mode == F_WRITE) {
    result = k_open_mode_write(fname, offset, &new_of, found, &entry);
  } else if (mode == F_APPEND) {
    result = k_open_mode_append(fname, offset, &new_of, found, &entry);
  } else {
    P_ERRNO = FS_INVALID_MODE;
    return -1;
  }
  if (result != FS_SUCCESS) {
    // allocation failed inside the mode function
    return -1;
  }
  GLOBAL_FD_TABLE[fd] = new_of;
  return fd;
}

ssize_t k_read(int fd, int n, char* buf) {
  if (fd == 0) {  // Check for STDIN
    return read(0, buf, n);
  }

  open_file_t* file_data = GLOBAL_FD_TABLE[fd];

  if (!(file_data->flag & F_READ)) {
    P_ERRNO = FS_NO_PERMISSION;
    return -1;
  }
  if (n <= 0)
    return 0;

  uint64_t current_offset = file_data->offset;
  uint16_t current_block_num = file_data->first_block;
  uint32_t file_size = file_data->size;

  if (current_offset >= file_size)
    return 0;
  if (current_offset + n > file_size) {
    n = file_size - current_offset;
  }
  ssize_t total_bytes_read = 0;

  size_t block_index = current_offset / FS_BLOCK_SIZE;
  size_t bytes_in_block = current_offset % FS_BLOCK_SIZE;

  // Find the actual starting block number for the current offset
  for (size_t i = 0; i < block_index; i++) {
    current_block_num = FAT_TABLE[current_block_num];
    // If hit EOF while skipping, the offset is invalid
    if (current_block_num == 0xFFFF || current_block_num == 0) {
      P_ERRNO = FS_INVALID_OFFSET;
      return -1;
    }
  }

  while (total_bytes_read < n) {
    if (current_block_num == 0xFFFF)
      break;
    off_t block_disk_offset =
        FS_FAT_SIZE + (current_block_num - 1) * FS_BLOCK_SIZE;

    size_t block_remaining = FS_BLOCK_SIZE - bytes_in_block;
    size_t requested_remaining = n - total_bytes_read;
    size_t bytes_to_read = (block_remaining < requested_remaining)
                               ? block_remaining
                               : requested_remaining;

    ssize_t bytes_read_this_step =
        pread(FS_HOST_FD, buf + total_bytes_read, bytes_to_read,
              block_disk_offset + bytes_in_block);

    if (bytes_read_this_step <= 0) {
      return (total_bytes_read > 0) ? total_bytes_read : bytes_read_this_step;
    }

    total_bytes_read += bytes_read_this_step;

    if (total_bytes_read < n) {
      current_block_num = FAT_TABLE[current_block_num];
      bytes_in_block = 0;
    }
  }
  file_data->offset += total_bytes_read;

  return total_bytes_read;
}

ssize_t k_write(int fd, const char* str, int n) {
  if (fd == 1 || fd == 2) {  // Check for STDOUT (1) or STDERR (2)
    return write(fd, str, n);
  }
  open_file_t* file_data = GLOBAL_FD_TABLE[fd];

  if (!(file_data->flag & (F_WRITE | F_APPEND))) {
    P_ERRNO = FS_NO_PERMISSION;
    return -1;
  }
  if (n <= 0) {
    return 0;
  }

  uint64_t current_offset = file_data->offset;
  uint16_t current_block_num = file_data->first_block;
  uint32_t old_file_size = file_data->size;

  ssize_t total_bytes_written = 0;

  size_t block_index = current_offset / FS_BLOCK_SIZE;
  size_t byte_in_block = current_offset % FS_BLOCK_SIZE;

  bool check_has_block = false;
  if (byte_in_block == 0 && current_offset > 0) {
    check_has_block = true;
  }
  // Traverse to the starting block
  if (current_block_num != 0) {  // Only traverse if the file has blocks
    for (size_t i = 0; i < block_index; i++) {
      if (check_has_block && i == block_index - 1 &&
          FAT_TABLE[current_block_num] == 0xFFFF) {
        block_index -= 1;
        byte_in_block = FS_BLOCK_SIZE;
        break;
      }

      current_block_num = FAT_TABLE[current_block_num];
      // If hit EOF while skipping, the offset is invalid
      if (current_block_num == 0xFFFF || current_block_num == 0) {
        P_ERRNO = FS_INVALID_OFFSET;
        return -1;
      }
    }
  }

  while (total_bytes_written < n) {
    uint16_t next_block_num;
    //  Check if current block needs allocation/extension
    if (current_block_num == 0 || byte_in_block == FS_BLOCK_SIZE) {
      // Find a free block
      next_block_num = k_find_free_block();
      if (next_block_num == 0) {
        // Disk full, stop writing
        k_write(1, "Disk is full\n", strlen("Disk is full\n"));
        break;
      }

      if (current_block_num == 0) {
        // This is the first block being written
        file_data->first_block = next_block_num;
        k_update_dirent(file_data);
      } else {
        FAT_TABLE[current_block_num] = next_block_num;
      }

      // Set up the new block's metadata
      current_block_num = next_block_num;
      FAT_TABLE[current_block_num] = 0xFFFF;
      byte_in_block = 0;
    }

    // Calculate Write Size
    size_t block_remaining = FS_BLOCK_SIZE - byte_in_block;
    size_t requested_remaining = n - total_bytes_written;
    size_t bytes_to_write = (block_remaining < requested_remaining)
                                ? block_remaining
                                : requested_remaining;

    // Write
    off_t block_disk_offset =
        FS_FAT_SIZE + (current_block_num - 1) * FS_BLOCK_SIZE;

    ssize_t bytes_written_this_step = pwrite(
        FS_HOST_FD,
        str + total_bytes_written,  // Buffer offset
        bytes_to_write,
        block_disk_offset + byte_in_block  // Disk offset + byte within block
    );

    if (bytes_written_this_step <= 0) {
      // Error, return bytes written so far (or error code if nothing written)
      return (total_bytes_written > 0) ? total_bytes_written
                                       : bytes_written_this_step;
    }

    // D. Update Counters
    total_bytes_written += bytes_written_this_step;
    byte_in_block += bytes_written_this_step;

    // Move to the next block in the FAT chain if necessary

    // if (byte_in_block == FS_BLOCK_SIZE) {
    //   current_block_num = FAT_TABLE[current_block_num];
    // }
  }

  file_data->offset += total_bytes_written;

  // Update size if the offset grew beyond the old file size
  if (file_data->offset > old_file_size) {
    file_data->size = file_data->offset;
    k_update_dirent(file_data);
  }

  return total_bytes_written;
}

int k_close(int kfd) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }
  if (kfd < 0 || kfd >= MAX_FD || GLOBAL_FD_TABLE[kfd] == NULL) {
    P_ERRNO = FS_BAD_FD;
    return -1;
  }

  open_file_t* of = GLOBAL_FD_TABLE[kfd];

  // 0/1/2 (STDIN/STDOUT/STDERR)
  if (kfd >= 0 && kfd <= 2) {
    free(of);
    GLOBAL_FD_TABLE[kfd] = NULL;
    return FS_SUCCESS;
  }

  // remove of entry from gdt so that later we can safely traverse through gdt
  // to search for entries of same file.
  GLOBAL_FD_TABLE[kfd] = NULL;

  off_t dirent_off = of->dirent_offset;
  dir_entry_t entry;
  ssize_t n = pread(FS_HOST_FD, &entry, sizeof(entry), dirent_off);
  if (n != (ssize_t)sizeof(entry)) {  // really should not happen
    free(of);
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }

  if (of->flag & (F_WRITE | F_APPEND)) {
    entry.size = of->size;
    entry.mtime = time(NULL);
    // note: k_write() should keep these information updated.
  }

  // entry.name[0] == 2 means this file has been unlinked but there still are
  // some fds referencing it (deleted but still in use). Therefore we need to
  // check if the current fd we're closing is the last one referencing it. If
  // so, we can truly mark this file as deleted (name[0] == 1).
  if (entry.name[0] == 2) {
    if (!k_is_file_still_open(dirent_off)) {
      k_free_fat_chain(entry.firstBlock);
      entry.name[0] = 1;
    }
    // If this is not the last fd referencing the file, do nothing
  }

  // write the entry back to disk
  n = pwrite(FS_HOST_FD, &entry, sizeof(entry), dirent_off);
  if (n != (ssize_t)sizeof(entry)) {  // again, this shouldn't happen
    free(of);
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }

  free(of);
  return FS_SUCCESS;
}

int k_unlink(const char* fname) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  off_t dirent_off;
  bool found = k_find_file(fname, &dirent_off);
  if (!found) {
    P_ERRNO = FS_FILE_NOT_FOUND;
    return -1;
  }

  dir_entry_t entry;
  ssize_t n = pread(FS_HOST_FD, &entry, sizeof(entry), dirent_off);
  if (n != (ssize_t)sizeof(entry)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }
  if (entry.type == 2) {  // directory
    P_ERRNO = FS_NOT_A_FILE;
    return -1;
  }

  // See if this file is still referenced by some other fds.
  if (k_is_file_still_open(dirent_off)) {
    // deleted-but-still-in-use. Mark as 2.
    entry.name[0] = 2;
    n = pwrite(FS_HOST_FD, &entry, sizeof(entry), dirent_off);
    if (n != (ssize_t)sizeof(entry)) {
      P_ERRNO = FS_IO_ERROR;
      return -1;
    }
  } else {
    // No other fds using this file, can do the cleaning.
    k_free_fat_chain(entry.firstBlock);
    entry.name[0] = 1;  // mark as 1.
    n = pwrite(FS_HOST_FD, &entry, sizeof(entry), dirent_off);
    if (n != (ssize_t)sizeof(entry)) {
      P_ERRNO = FS_IO_ERROR;
      return -1;
    }
  }
  return FS_SUCCESS;
}

int k_lseek(int kfd, int offset, int whence) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }
  if (kfd < 0 || kfd >= MAX_FD || GLOBAL_FD_TABLE[kfd] == NULL) {
    P_ERRNO = FS_BAD_FD;
    return -1;
  }

  open_file_t* of = GLOBAL_FD_TABLE[kfd];

  // calculate the new offset based on whence mode.
  int64_t new_pos;
  switch (whence) {
    case F_SEEK_SET:
      new_pos = offset;
      break;
    case F_SEEK_CUR:
      new_pos = (int64_t)of->offset + offset;
      break;
    case F_SEEK_END:
      new_pos = (int64_t)of->size + offset;
      break;
    default:
      P_ERRNO = FS_INVALID_WHENCE;
      return -1;
  }

  if (new_pos < 0) {
    P_ERRNO = FS_INVALID_OFFSET;
    return -1;
  }

  // Update the file's size if file is opened in write mode. Note that this only
  // updates the file's metadata in the open file table. The actual disk space
  // allocation should happen in k_write, while writing the updated metadata
  // back to disk dirent should happen in k_close. Therefore, k_read and k_write
  // must stick to metadata from open file entries, not dirent on disk.
  if (new_pos > of->size && (of->flag & (F_WRITE | F_APPEND))) {
    of->size = (uint32_t)new_pos;
  }

  of->offset = (uint64_t)new_pos;
  return FS_SUCCESS;
}

void k_format_dirent(const dir_entry_t* entry, char* buffer, size_t size) {
  if (strcmp(entry->name, ".") == 0) {
    if (size > 0)
      buffer[0] = '\0';
    return;
  }

  int pos = 0;

  if (entry->firstBlock == 0) {  // file not actually allocated yet.
    pos += snprintf(buffer + pos, size - pos, "      ");
  } else {
    pos += snprintf(buffer + pos, size - pos, "%5u ", entry->firstBlock);
  }

  char mode[5] = "----";
  if (entry->type == 2)
    mode[0] = 'd';
  if (entry->perm & 4)
    mode[1] = 'r';
  if (entry->perm & 2)
    mode[2] = 'w';
  if (entry->perm & 1)
    mode[3] = 'x';
  pos += snprintf(buffer + pos, size - pos, "%s ", mode);

  pos += snprintf(buffer + pos, size - pos, "%10u ", entry->size);

  time_t t = (time_t)entry->mtime;
  struct tm tm;
  localtime_r(&t, &tm);
  char timebuf[32];
  strftime(timebuf, sizeof(timebuf), "%b %e %H:%M:%S %Y", &tm);
  pos += snprintf(buffer + pos, size - pos, "%s ", timebuf);

  snprintf(buffer + pos, size - pos, "%s\n", entry->name);
}

int k_scan_dir(const char* filename, void (*callback)(const dir_entry_t*)) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  // If given a filename: attempt to process it directly
  if (filename != NULL) {
    off_t dirent_off;
    bool found = k_find_file(filename, &dirent_off);
    if (!found) {
      P_ERRNO = FS_FILE_NOT_FOUND;
      return -1;
    }
    dir_entry_t entry;
    if (pread(FS_HOST_FD, &entry, sizeof(entry), dirent_off) !=
        (ssize_t)sizeof(entry)) {
      P_ERRNO = FS_IO_ERROR;
      return -1;
    }

    if (callback)
      callback(&entry);
    return FS_SUCCESS;
  }

  // If no filename given: list all files in current directory (root)
  uint16_t blknum = 1;  // root firstBlock
  dir_entry_t entry;

  while (blknum != 0xFFFF) {
    for (int i = 0; i < FS_ENTRY_PER_BLK; i++) {
      off_t off =
          FS_FAT_SIZE + (blknum - 1) * FS_BLOCK_SIZE + i * sizeof(dir_entry_t);

      if (pread(FS_HOST_FD, &entry, sizeof(entry), off) !=
          (ssize_t)sizeof(entry)) {
        P_ERRNO = FS_IO_ERROR;
        return -1;
      }

      if (entry.name[0] == 0) {
        // finished
        return FS_SUCCESS;
      }
      if (entry.name[0] == 1 || entry.name[0] == 2) {
        continue;
      }

      if (callback)
        callback(&entry);
    }

    blknum = FAT_TABLE[blknum];
  }

  return FS_SUCCESS;
}

int k_ls(const char* filename) {
  return k_scan_dir(filename, k_print_dirent);
}

int k_cat(char** args) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  int output_fd = 1;
  int output_mode = 0;  // 0 = stdout, 1 = -w, 2 = -a
  int input_start_index = 1;
  int input_end_index =
      -1;  // Marks the index BEFORE -w/-a, or the last arg index + 1
  char* output_fname = NULL;

  for (int i = 1; args[i] != NULL; i++) {
    if (strcmp(args[i], "-w") == 0 || strcmp(args[i], "-a") == 0) {
      output_mode = (strcmp(args[i], "-w") == 0) ? 1 : 2;
      output_fname = args[i + 1];  // Output filename is the next argument

      if (output_fname == NULL) {
        P_ERRNO = FS_FILE_NOT_FOUND;
        return -1;
      }
      input_end_index = i;
      break;
    }
  }

  if (input_end_index == -1) {
    for (int i = 1; args[i] != NULL; i++) {
      input_end_index = i + 1;
    }
    if (input_end_index == 1) {
      // Case: "cat" with no arguments. Default to copying STDIN to STDOUT.
      input_start_index = 0;
      input_end_index = 1;
    }
  }

  // Check for "cat -w OUT" or "cat -a OUT" (No input files specified)
  // If input_start_index (1) equals input_end_index, the only input is STDIN.
  bool reading_from_stdin = (input_start_index == input_end_index);

  if (output_mode != 0) {
    int target_mode = (output_mode == 1) ? F_WRITE : F_APPEND;

    output_fd = k_open(output_fname, target_mode);
    if (output_fd < 0) {
      P_ERRNO = FS_IO_ERROR;
      return -1;
    }
  }

  int success = FS_SUCCESS;

  if (reading_from_stdin) {
    success = (copy_stream_content(0, output_fd) == 0) ? 0 : -1;
    if (success == -1)
      P_ERRNO = FS_IO_ERROR;
  } else {
    for (int i = input_start_index; i < input_end_index; i++) {
      char* input_fname = args[i];

      int input_fd = k_open(input_fname, F_READ);
      if (input_fd < 0) {
        success = -1;  // Propagate error from k_open
        continue;
      }

      if (copy_stream_content(input_fd, output_fd) != 0) {
        success = -1;
        P_ERRNO = FS_IO_ERROR;
      }

      if (k_close(input_fd) < 0)
        success = -1;
    }
  }

  if (output_mode != 0) {
    if (k_close(output_fd) < 0)
      success = -1;
  }

  return success;
}

int k_chmod_update(const char* fname, uint8_t new_perm) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  off_t dir_entry_disk_offset = 0;

  if (!k_find_file(fname, &dir_entry_disk_offset)) {
    P_ERRNO = FS_FILE_NOT_FOUND;
    return -1;
  }

  dir_entry_t entry;

  if (pread(FS_HOST_FD, &entry, sizeof(dir_entry_t), dir_entry_disk_offset) !=
      sizeof(dir_entry_t)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }

  // entry.perm = new_perm;

  uint8_t op_add = 0x80;     // Flag: add permission
  uint8_t op_remove = 0x40;  // Flag: remove permission
  uint8_t op_assign = 0x20;  // Flag: assign permission (=)
  uint8_t val_mask = 0x07;   // Valid permission bits (rwx)

  if (new_perm & op_add) {
    // Mode: + (add)
    // Keep original permissions and add new bits
    entry.perm |= (new_perm & val_mask);
  } else if (new_perm & op_remove) {
    // Mode: - (remove)
    // Keep original permissions and remove specified bits
    entry.perm &= ~(new_perm & val_mask);
  } else if (new_perm & op_assign) {
    // Mode: = (assign)
    // Directly set new permissions
    entry.perm = (new_perm & val_mask);
  } else {
    // Mode: numeric (assign)
    entry.perm = (new_perm & val_mask);
  }

  entry.mtime = time(NULL);

  if (pwrite(FS_HOST_FD, &entry, sizeof(dir_entry_t), dir_entry_disk_offset) !=
      sizeof(dir_entry_t)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }

  return FS_SUCCESS;
}

int k_check_executable(const char* fname) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  off_t dirent_off;
  bool found = k_find_file(fname, &dirent_off);
  if (!found) {
    P_ERRNO = FS_FILE_NOT_FOUND;
    return -1;
  }

  dir_entry_t entry;
  if (pread(FS_HOST_FD, &entry, sizeof(entry), dirent_off) != sizeof(entry)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }
  
  if (entry.type != 1) {
    P_ERRNO = FS_NOT_A_FILE;
    return -1;
  }

  if (!(entry.perm & 1)) {
    P_ERRNO = FS_NO_PERMISSION;
    return -1;
  }

  return FS_SUCCESS;
}


int k_mv(const char* source, const char* dest) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  off_t source_offset = 0;
  off_t dest_offset = 0;

  // check if the source file exists
  if (!k_find_file(source, &source_offset)) {
    P_ERRNO = FS_FILE_NOT_FOUND;
    return -1;
  }

  dir_entry_t source_dirent;
  if (pread(FS_HOST_FD, &source_dirent, sizeof(dir_entry_t), source_offset) !=
      sizeof(dir_entry_t)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }
  if (!(source_dirent.perm & 4)) {
    P_ERRNO = FS_NO_PERMISSION;
    return -1;
  }

  strncpy(source_dirent.name, dest, 31);
  source_dirent.name[31] = '\0';
  source_dirent.mtime = time(NULL);

  // check if the destination file already exists
  if (k_find_file(dest, &dest_offset)) {
    dir_entry_t dest_dirent;
    if (pread(FS_HOST_FD, &dest_dirent, sizeof(dir_entry_t), dest_offset) !=
        sizeof(dir_entry_t)) {
      P_ERRNO = FS_IO_ERROR;
      return -1;
    }
    if (!(dest_dirent.perm & 2)) {
      P_ERRNO = FS_NO_PERMISSION;
      return -1;
    }
    int result = k_unlink(dest);
    if (result < 0) {
      return result;
    }
  }

  if (pwrite(FS_HOST_FD, &source_dirent, sizeof(dir_entry_t), source_offset) !=
      sizeof(dir_entry_t)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }

  return FS_SUCCESS;
}

int k_cp(char** args) {
  if (!IS_FS_MOUNTED) {
    P_ERRNO = FS_NOT_MOUNTED;
    return -1;
  }

  const char* source_path = NULL;
  const char* dest_path = NULL;
  int mode = 0;  // 0=P2P, 1=H2P, 2=P2H

  if (args[1] && strcmp(args[1], "-h") == 0) {
    // Mode 1: Host to PennFAT (H2P) -> cp -h SOURCE DEST
    if (!args[2] || !args[3]) {
      P_ERRNO = FS_INVALID_ARG;
      return -1;
    }
    source_path = args[2];
    dest_path = args[3];
    mode = 1;
  } else if (args[2] && strcmp(args[2], "-h") == 0) {
    // Mode 2: PennFAT to Host (P2H) -> cp SOURCE -h DEST
    if (!args[1] || !args[3]) {
      P_ERRNO = FS_INVALID_ARG;
      return -1;
    }
    source_path = args[1];
    dest_path = args[3];
    mode = 2;
  } else {
    // Mode 0: PennFAT to PennFAT (P2P) -> cp SOURCE DEST
    source_path = args[1];
    dest_path = args[2];
    mode = 0;
  }
  if (!source_path || !dest_path) {
    P_ERRNO = FS_INVALID_ARG;
    return -1;
  }

  int result = 0;

  if (mode == 0) {
    // P2P: PennFAT to PennFAT (Uses k_open/k_read/k_write)
    int src_fd = k_open(source_path, F_READ);
    if (src_fd < 0) {
      // P_ERRNO is set by k_open
      return -1;
    }

    // Open destination for writing, which creates/truncates the file.
    int dest_fd = k_open(dest_path, F_WRITE);
    if (dest_fd < 0) {
      k_close(src_fd);
      return -1;
    }

    result = (copy_stream_content(src_fd, dest_fd) == 0) ? 0 : -1;
    if (result == -1)
      P_ERRNO = FS_IO_ERROR;

    int res1 = k_close(src_fd);
    int res2 = k_close(dest_fd);
    if (res1 < 0) {
      result = res1;
    }
    if (res2 < 0) {
      result = res2;
    }

  } else if (mode == 1) {
    // H2P: Host to PennFAT (Uses HOST read and KERNEL k_write)
    int dest_fd = k_open(dest_path, F_WRITE);
    if (dest_fd < 0) {
      return -1;
    }
    result =
        (k_host_read_to_pennfat_write(source_path, dest_fd) == 0) ? result : -1;
    if (result == -1 && P_ERRNO == P_NO_ERR)
      P_ERRNO = FS_IO_ERROR;  // Ensure P_ERRNO set
    int res = k_close(dest_fd);
    if (res < 0) {
      result = res;
    }

  } else if (mode == 2) {
    // P2H: PennFAT to Host (Uses KERNEL k_read and HOST write)
    int src_fd = k_open(source_path, F_READ);
    if (src_fd < 0) {
      return -1;
    }
    result =
        (k_pennfat_read_to_host_write(src_fd, dest_path) == 0) ? result : -1;
    if (result == -1 && P_ERRNO == P_NO_ERR)
      P_ERRNO = FS_IO_ERROR;
    int res = k_close(src_fd);
    if (res < 0) {
      result = res;
    }
  }

  return result;
}

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Implementations =================== //
////////////////////////////////////////////////////////////////////////////

static void k_gdt_init(void) {
  // for STDIN (0), STDOUT (1), STDERR (2)
  for (int i = 0; i <= 2; i++) {
    open_file_t* std_file = (open_file_t*)malloc(sizeof(open_file_t));
    if (std_file == NULL) {
      perror("Failed to allocate memory for file descriptors");
      exit(EXIT_FAILURE);
    }
    open_file_init(std_file);
    if (i == 0) {
      strcpy(std_file->name, "STDIN");
      std_file->flag = F_READ;
    } else if (i == 1) {
      strcpy(std_file->name, "STDOUT");
      std_file->flag = F_WRITE;
    } else {
      strcpy(std_file->name, "STDERR");
      std_file->flag = F_WRITE;
    }

    GLOBAL_FD_TABLE[i] = std_file;
  }
}

static void k_gdt_cleanup(void) {
  for (int i = 0; i < MAX_FD; i++) {
    if (GLOBAL_FD_TABLE[i] != NULL) {
      free(GLOBAL_FD_TABLE[i]);
      GLOBAL_FD_TABLE[i] = NULL;
    }
  }
}

static off_t k_extend_root() {
  uint16_t blknum = 1;
  uint16_t last_blk = 1;
  while (blknum != 0xFFFF) {
    last_blk = blknum;
    blknum = FAT_TABLE[blknum];
  }
  // Above is just to locate the last block of root directory.

  for (int i = 1; i < FS_NUM_ENTRIES; i++) {
    if (FAT_TABLE[i] == 0x0000) {
      // Update the FAT.
      FAT_TABLE[last_blk] = i;
      FAT_TABLE[i] = 0xFFFF;
      // Cleanse the block
      char zero_buf[FS_BLOCK_SIZE];
      memset(zero_buf, 0, FS_BLOCK_SIZE);
      // calculate new block offset
      off_t off = FS_FAT_SIZE + (i - 1) * FS_BLOCK_SIZE;
      pwrite(FS_HOST_FD, zero_buf, FS_BLOCK_SIZE, off);
      // Since we have a new block, the new dirent offset will be the same as
      // block offset (first entry).
      return off;
    }
  }

  // We literally have no space left in the file system (what have we done).
  return (off_t)-1;
}

static int k_find_free_block() {
  for (int i = 1; i < FS_NUM_ENTRIES; i++) {
    if (FAT_TABLE[i] == 0x0000) {
      return i;
    }
  }
  return 0;
}

static int k_find_gdt_spot() {
  int fd = -1;
  for (int i = 1; i < MAX_GDT_ENTRY; i++) {
    if (GLOBAL_FD_TABLE[i] == NULL) {
      fd = i;
      break;
    }
  }

  return fd;
}

static bool k_have_write_opened(const char* fname) {
  int write_instances = 0;
  (void)write_instances;  // NOTE: compiler warned this variable as unused

  // iterate through  GDT slots
  for (int i = 3; i < MAX_GDT_ENTRY; i++) {
    open_file_t* of = GLOBAL_FD_TABLE[i];
    if (of != NULL) {
      if (strcmp(of->name, fname) == 0) {
        if (of->flag & (F_WRITE | F_APPEND)) {
          return true;
        }
      }
    }
  }
  return false;
}

static void k_free_fat_chain(uint16_t first_block) {
  uint16_t blk = first_block;
  while (blk != 0 && blk != 0xFFFF) {
    uint16_t next = FAT_TABLE[blk];
    FAT_TABLE[blk] = 0x0000;
    blk = next;
  }
}

static int k_update_dirent(open_file_t* file_data) {
  dir_entry_t entry;
  ssize_t written_bytes;

  if (pread(FS_HOST_FD, &entry, sizeof(dir_entry_t),
            file_data->dirent_offset) != sizeof(dir_entry_t)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }

  entry.firstBlock = file_data->first_block;
  entry.size = (uint32_t)file_data->size;
  entry.mtime = time(NULL);
  written_bytes =
      pwrite(FS_HOST_FD, &entry, sizeof(dir_entry_t), file_data->dirent_offset);

  if (written_bytes != sizeof(dir_entry_t)) {
    P_ERRNO = FS_IO_ERROR;
    return -1;
  }

  return FS_SUCCESS;
}

static bool k_is_file_still_open(off_t dirent_offset) {
  for (int i = 0; i < MAX_GDT_ENTRY; i++) {
    open_file_t* of = GLOBAL_FD_TABLE[i];
    if (of && of->dirent_offset == dirent_offset) {
      return true;
    }
  }
  return false;
}

static void k_print_dirent(const dir_entry_t* entry) {
  char buffer[256];
  k_format_dirent(entry, buffer, sizeof(buffer));
  if (strlen(buffer) > 0) {
    k_write(1, buffer, strlen(buffer));
  }
}

static int k_open_mode_read(const char* fname,
                            off_t offset,
                            open_file_t** new_of_out,
                            bool found,
                            dir_entry_t* entry) {
  if (!found) {
    P_ERRNO = FS_FILE_NOT_FOUND;
    return -1;
  }

  pread(FS_HOST_FD, entry, sizeof(dir_entry_t), offset);

  if (entry->type != 1) {
    P_ERRNO = FS_NOT_A_FILE;
    return -1;
  }

  if (!(entry->perm & 4)) {
    P_ERRNO = FS_NO_PERMISSION;
    return -1;
  }

  *new_of_out = (open_file_t*)malloc(sizeof(open_file_t));
  if (*new_of_out == NULL) {
    P_ERRNO = FS_MALLOC_FAIL;
    return -1;
  }

  open_file_init(*new_of_out);

  strncpy((*new_of_out)->name, fname, 31);
  (*new_of_out)->name[31] = '\0';
  (*new_of_out)->size = entry->size;
  (*new_of_out)->perm = entry->perm;
  (*new_of_out)->first_block = entry->firstBlock;
  (*new_of_out)->dirent_offset = offset;
  (*new_of_out)->flag = F_READ;

  return FS_SUCCESS;
}

static int k_open_mode_write(const char* fname,
                             off_t offset,
                             open_file_t** new_of_out,
                             bool found,
                             dir_entry_t* entry) {
  if (!found) {
    // create a new directory entry structure in memory
    memset(entry, 0, sizeof(dir_entry_t));
    strncpy(entry->name, fname, 31);
    entry->name[31] = '\0';
    entry->type = 1;  // Regular File
    entry->perm = 6;  // Read and Write

    // Write the new directory entry to disk
    if (pwrite(FS_HOST_FD, entry, sizeof(dir_entry_t), offset) !=
        sizeof(dir_entry_t)) {
      P_ERRNO = FS_IO_ERROR;
      return -1;
    }
  } else {
    pread(FS_HOST_FD, entry, sizeof(dir_entry_t), offset);
    if (entry->type != 1) {
      P_ERRNO = FS_NOT_A_FILE;
      return -1;
    }

    if (!(entry->perm & 2)) {
      P_ERRNO = FS_NO_PERMISSION;
      return -1;
    }

    if (entry->size > 0) {
      k_free_fat_chain(entry->firstBlock);
      entry->size = 0;
      entry->firstBlock = 0;
      entry->mtime = time(NULL);

      // Write the truncated entry back to disk
      if (pwrite(FS_HOST_FD, entry, sizeof(dir_entry_t), offset) !=
          sizeof(dir_entry_t)) {
        P_ERRNO = FS_IO_ERROR;
        return -1;
      }
    }
  }

  *new_of_out = (open_file_t*)malloc(sizeof(open_file_t));
  if (*new_of_out == NULL) {
    P_ERRNO = FS_MALLOC_FAIL;
    return -1;
  }

  open_file_init(*new_of_out);
  strncpy((*new_of_out)->name, fname, 31);
  (*new_of_out)->name[31] = '\0';
  (*new_of_out)->first_block = entry->firstBlock;
  (*new_of_out)->dirent_offset = offset;
  (*new_of_out)->flag = F_WRITE;
  (*new_of_out)->offset = 0;
  (*new_of_out)->perm = entry->perm;

  return FS_SUCCESS;
}

static int k_open_mode_append(const char* fname,
                              off_t offset,
                              open_file_t** new_of_out,
                              bool found,
                              dir_entry_t* entry) {
  if (!found) {
    memset(entry, 0, sizeof(dir_entry_t));
    strncpy(entry->name, fname, 31);
    entry->name[31] = '\0';
    entry->type = 1;
    entry->perm = 6;
    if (pwrite(FS_HOST_FD, entry, sizeof(dir_entry_t), offset) !=
        sizeof(dir_entry_t)) {
      P_ERRNO = FS_IO_ERROR;
      return -1;
    }
  } else {
    pread(FS_HOST_FD, entry, sizeof(dir_entry_t), offset);
    if (entry->type != 1) {
      P_ERRNO = FS_NOT_A_FILE;
      return -1;
    }
    if (!(entry->perm & 2)) {
      P_ERRNO = FS_NO_PERMISSION;
      return -1;
    }
  }

  *new_of_out = (open_file_t*)malloc(sizeof(open_file_t));
  if (*new_of_out == NULL) {
    P_ERRNO = FS_MALLOC_FAIL;
    return -1;
  }

  open_file_init(*new_of_out);
  strncpy((*new_of_out)->name, fname, 31);
  (*new_of_out)->name[31] = '\0';
  (*new_of_out)->first_block = entry->firstBlock;
  (*new_of_out)->dirent_offset = offset;
  (*new_of_out)->flag = F_APPEND;
  (*new_of_out)->perm = entry->perm;

  // set offset to the current file size
  (*new_of_out)->offset = entry->size;

  return FS_SUCCESS;
}

static int k_host_read_to_pennfat_write(const char* host_path, int pennfat_fd) {
  int host_fd = open(host_path, O_RDONLY);
  if (host_fd == -1) {
    perror("cp: Cannot open host source file");
    return -1;
  }

  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;
  int result = 0;

  while ((bytes_read = read(host_fd, buffer, BUFFER_SIZE)) > 0) {
    // Use k_write on pennfat_fd
    ssize_t bytes_written = k_write(pennfat_fd, buffer, bytes_read);
    if (bytes_written != bytes_read) {
      {
        char buf[128];
        int len =
            snprintf(buf, sizeof(buf),
                     "cp: Error writing to PennFAT destination. %ld, %ld\n",
                     bytes_written, bytes_read);
        k_write(2, buf, len);
      }
      result = -1;
      break;
    }
  }

  if (bytes_read == -1) {
    perror("Error reading from host source");
    result = -1;
  }

  close(host_fd);
  return result;
}

static int k_pennfat_read_to_host_write(int pennfat_fd, const char* host_path) {
  // Open/Create the destination file on the host OS
  int host_fd = open(host_path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if (host_fd == -1) {
    perror("cp: Cannot create/open host destination file");
    return -1;
  }

  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;
  int result = 0;

  while ((bytes_read = k_read(pennfat_fd, BUFFER_SIZE, buffer)) > 0) {
    ssize_t bytes_written = write(host_fd, buffer, bytes_read);
    if (bytes_written != bytes_read) {
      perror("cp: Error writing to host destination");
      result = -1;
      break;
    }
  }

  if (bytes_read < 0) {
    k_write(2, "cp: Error reading from PennFAT source.\n",
            strlen("cp: Error reading from PennFAT source.\n"));
    result = -1;
  }

  close(host_fd);
  return result;
}

static int copy_stream_content(int input_fd, int output_fd) {
  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;
  ssize_t bytes_written;
  int result = 0;

  while (1) {
    bytes_read = k_read(input_fd, BUFFER_SIZE, buffer);

    if (bytes_read < 0) {
      {
        char buf[128];
        int len =
            snprintf(buf, sizeof(buf),
                     "cat: Error reading input stream (FD %d).\n", input_fd);
        k_write(2, buf, len);
      }
      result = -1;
      break;
    }
    if (bytes_read == 0) {
      break;
    }

    bytes_written = k_write(output_fd, buffer, bytes_read);

    if (bytes_written < 0 || bytes_written != bytes_read) {
      {
        char buf[128];
        int len = snprintf(
            buf, sizeof(buf),
            "cat: Error writing to output destination (FD %d).\n", output_fd);
        k_write(2, buf, len);
      }
      result = -1;
      break;
    }
  }
  return result;
}