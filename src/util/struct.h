#ifndef STRUCT_H_
#define STRUCT_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include "./Vec.h"
#include "./spthread.h"

#define NUM_PRIO 3
#define MAX_FD 32
#define MAX_NAME_LEN 32
#define MAX_PROC 1024
#define PID_INVALID 0
#define PID_INIT 1

/** PennOS process state */
typedef enum { P_READY, P_RUNNING, P_BLOCKED, P_STOPPED, P_ZOMBIE } pstate_t;
/** PennOS process exit status */
typedef enum {
  P_EXIT_NONE,
  P_EXIT_EXITED,
  P_EXIT_SIGNALED,
  P_EXIT_STOPPED
} pexit_t;

typedef struct open_file {
  char name[MAX_NAME_LEN];  // cached file name
  uint32_t size;            // cached file size
  uint8_t perm;             // cached file perm

  uint16_t first_block;  // fast reference to file block
  off_t dirent_offset;   // fast reference to dirent

  uint64_t offset;  // fd-specific offset
  uint8_t flag;     // fd-specific F_READ/F_WRITE/F_APPEND
} open_file_t;

typedef struct pcb {
  // Process identity
  spthread_t process;           // spthread handle for the process
  char cmd_name[MAX_NAME_LEN];  // Process name/command brief
  char** args;                  // Deep-copied arguments
  pid_t pid;
  pstate_t state;
  int prio;               // Priority: 0, 1, or 2
  int wake_tick;          // Used while sleeping (in clock ticks)
  bool stopped_reported;  // If STOPPED state has been reported to waitpid

  // Parent process
  pid_t ppid;
  struct pcb* parent;

  // Child processes
  Vec childs;

  // Local File Descriptor Table (Stores KFD index instead of a pointer)
  int fd_table[MAX_FD];

  // Exit status
  pexit_t exit_status;

} pcb_t;

/** @brief Process table */
typedef pcb_t* process_table[MAX_PROC];

/** @brief Priority queues */
typedef Vec priority_queues_t[NUM_PRIO];  // default 3

/** @brief Blocked queue */
typedef struct {
  Vec blocked_queue;  // Queue of blocked processes
} blocked_queue_t;

/**
 * @brief Initialize a PCB.
 *
 * @param pcb The PCB to initialize.
 */
void pcb_init(pcb_t* pcb);

/**
 * @brief Initialize an open file entry.
 *
 * @param file The open file entry to initialize.
 */
void open_file_init(open_file_t* file);

#endif  // STRUCT_H_
