# PennOS Group 5

## Personal Information
- Name: Ajax Li
- Penn Key: ajaxxx

- Name: Sau Lok Li
- Penn Key: li86

- Name:Tianshi Miao
- Penn Key: miaots

- Name: Feiyang Jin
- Penn Key: jin0

## List of Submitted Source Files
### src/
- `fat_kernel.c` / `fat_kernel.h`
- `fat_syscalls.c` / `fat_syscalls.h`
- `pennfat.c`
- `pennos.c`
- `process.c` / `process.h`
- `scheduler.c` / `scheduler.h`
- `syscall.c` / `syscall.h`
- `user_function.c` / `user_function.h`

### src/util/
- `Vec.c` / `Vec.h`
- `job.c` / `job.h`
- `p_errno.c` / `p_errno.h`
- `p_handler.c` / `p_handler.h`
- `p_signal.c` / `p_signal.h`
- `panic.c` / `panic.h`
- `parser.c` / `parser.h`
- `queue.c` / `queue.h`
- `spthread.c` / `spthread.h`
- `stress.c` / `stress.h`
- `struct.c` / `struct.h`

## Compilation Instructions
This project uses a `Makefile` for building. Please use the following commands in the project root directory:

1.  **Compile all targets:**
    ```bash
    make
    ```
    This will generate the `bin/pennos` and `bin/pennfat` executables.

2.  **Compile tests:**
    ```bash
    make tests
    ```
    This will generate test executables (e.g., `bin/sched-demo`).

3.  **Clean build files:**
    ```bash
    make clean
    ```
    This removes all object files and executables.

4.  **Format code:**
    ```bash
    make format
    ```

## Overview of Work Accomplished
This project implements PennOS, a simple operating system, consisting of the following main components:

1.  **PennFAT Filesystem**:
    - Implemented a comprehensive FAT-based filesystem kernel (`fat_kernel.c`).
    - Supports filesystem `mount` and `unmount` operations with global file descriptor table (GDT).
    - Supports standard file operations: `open` (read/write/append modes), `read`, `write`, `close`, `lseek`, `unlink`.
    - Additional file operations: `mv` (rename), `cp` (copy with host-to-PennFAT and PennFAT-to-host support), `chmod` (permission management).
    - Implements deferred deletion for files that are unlinked while still open.
    - Directory entry management with timestamps (mtime) and permission bits (rwx).
    - Free space management with FAT chain allocation and deallocation.
    - Process-local file descriptor tables with proper inheritance on `s_spawn`.
    - Provided a standalone tool `pennfat` for creating (`mkfs`) and manipulating filesystem images.

2.  **Process Scheduler**:
    - Implemented a weighted priority-based scheduler (`scheduler.c`).
    - Supports three priority queues (0=interactive, 1=normal, 2=batch) with 9:6:4 weighted random selection for fair scheduling.
    - Uses `SIGALRM` to implement preemptive time-sliced round-robin scheduling (100ms time quantum).
    - Manages complete process lifecycle (Ready, Running, Blocked, Stopped, Zombie).
    - Implements process state transitions with signal support (P_SIGTERM, P_SIGSTOP, P_SIGCONT, P_SIGCHLD).
    - Handles context switching using spthread library and idle state management.
    - Supports sleep functionality with blocked queue management and automatic wake-up.
    - Implements orphan adoption to init process and proper zombie reaping.

3.  **Shell and User Space**:
    - Implemented comprehensive Shell functionality supporting user interaction.
    - Supports foreground and background job control with `bg`, `fg`, and `jobs` commands.
    - Handles Ctrl-C (SIGINT) and Ctrl-Z (SIGTSTP) signals from host OS, mapping them to PennOS signals for foreground process control.
    - Supports I/O redirection (stdin/stdout) with `<`, `>`, and `>>` operators.
    - Implemented extensive shell commands:
      - **Process Management**: `ps`, `kill`, `nice`, `nice_pid`, `sleep`, `busy`
      - **File System**: `cat`, `echo`, `ls`, `touch`, `mv`, `cp`, `rm`, `chmod`
      - **Job Control**: `jobs`, `bg`, `fg`
      - **Utilities**: `man`, `logout`
      - **Testing**: `zombify`, `orphanify`, `hang`, `nohang`, `recur`, `crash` (stress test utilities)

4.  **System Calls**:
    - Encapsulated comprehensive system call interfaces:
      - **Filesystem Operations**: `s_open`, `s_read`, `s_write`, `s_close`, `s_lseek`, `s_unlink`, `s_ls`, `s_cat`, `s_mv`, `s_cp`, `s_check_executable`, `s_chmod`
      - **Process Management**: `s_spawn`, `s_waitpid`, `s_kill`, `s_exit`, `s_nice`, `s_sleep`, `s_getpid`, `s_get_all_process`, `s_shutdown`
    - Proper error handling with global `P_ERRNO` variable and comprehensive error codes.
    - Support for file descriptor inheritance and I/O redirection in process spawning.

## Description of Code and Code Layout
The code is primarily organized in the `src` directory, with general utility functions located in `src/util`.

### Core Files:
-   **`src/pennos.c`**: The entry point of the OS. Responsible for kernel initialization (`k_init`), mounting the filesystem, spawning the init process, and starting the scheduler loop (`k_scheduler_run`).
-   **`src/pennfat.c`**: The entry point for the standalone FAT filesystem tool, used to manipulate filesystem images without starting the full OS.
-   **`src/fat_kernel.c/h`**: Core implementation of the FAT filesystem. Contains `k_open`, `k_read`, `k_write`, `mkfs`, `mount`, etc.
-   **`src/scheduler.c/h`**: Core implementation of the scheduler. Contains the scheduling loop, context switching logic, and `SIGALRM` signal handling.
-   **`src/process.c/h`**: Definitions for the Process Control Block (PCB) and process management functions (e.g., `k_process_create`, `k_process_kill`).
-   **`src/syscall.c/h`**: Defines the system call interfaces available to user space.
-   **`src/fat_syscalls.c/h`**: Wrappers for filesystem-related system calls.
-   **`src/user_function.c/h`**: Implementation of built-in Shell commands and user-space program logic.

### Utilities (src/util/):
-   **`queue.c/h`**: Process queue management for ready queues (3 priority levels), blocked queue, and queue operations (enqueue, dequeue, block, unblock, stop, continue).
-   **`Vec.c/h`**: Dynamic array (vector) implementation for managing children lists and queue storage.
-   **`parser.c/h`**: Command-line argument parsing with support for I/O redirection operators (`<`, `>`, `>>`).
-   **`job.c/h`**: Job control logic for background/foreground job management, job listing, and state tracking.
-   **`p_errno.c/h`**: PennOS error code definitions and error handling (P_ERRNO global variable).
-   **`p_signal.c/h`**: Signal handling for PennOS signals (P_SIGTERM, P_SIGSTOP, P_SIGCONT, P_SIGCHLD).
-   **`p_handler.c/h`**: Host signal handler for Ctrl-C and Ctrl-Z, mapping to foreground process control.
-   **`struct.c/h`**: Core data structure definitions (PCB, open_file_t, directory entry).
-   **`panic.c/h`**: Error handling and panic mechanisms.
-   **`spthread.c/h`**: User-level threading library for process context switching.
-   **`stress.c/h`**: Stress testing utilities (`hang`, `nohang`, `recur`, `crash`) for testing scheduler and process management.

## General Comments
-   **Logging**: The kernel supports comprehensive event logging to `log/log.txt` (or custom log file). Logs include scheduler events (CREATE, SCHEDULE, BLOCKED, UNBLOCKED, STOPPED, CONTINUED, EXITED, SIGNALED, ZOMBIE, WAITED, ORPHAN, NICE) for debugging and performance analysis.
-   **Error Handling**: Robust error handling with `P_ERRNO` global variable and human-readable error messages via `u_perror()`.
-   **Resource Management**: All system resources (open file descriptors, process PCBs, memory allocations) are properly cleaned up upon kernel shutdown to prevent memory leaks.
-   **Init Process**: PennOS uses an init process (PID 1) that spawns and manages the shell, automatically restarting it on crash and adopting orphaned processes.
-   **Signal Handling**: Deferred signal processing in scheduler loop to safely handle host signals (Ctrl-C, Ctrl-Z) without race conditions.
-   **File Descriptor Inheritance**: Child processes inherit parent's file descriptors, with support for per-process redirection during spawn.
