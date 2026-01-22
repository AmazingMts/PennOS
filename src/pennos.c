#include <stdio.h>
#include "./util/struct.h"
#include "fat_kernel.h"
#include "process.h"
#include "scheduler.h"

// Initialize all kernel data structures (queues, tables, FAT mount)
static void k_init(const char* fatfs_name, const char* log_fname) {
  // Initialize scheduler (which will initialize queues)
  k_scheduler_init(log_fname);

  // Mount FAT filesystem
  if (mount(fatfs_name) != FS_SUCCESS) {
    fprintf(stderr, "Failed to mount filesystem: %s\n", fatfs_name);
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <fatfs_name> [log_fname]\n", argv[0]);
    return 1;
  }

  const char* fatfs_name = argv[1];
  const char* log_fname = (argc >= 3) ? argv[2] : NULL;

  // Initialize kernel with filesystem and log file
  k_init(fatfs_name, log_fname);

  // Start the init process (which will spawn the shell)
  k_start_init_process();

  // Run the scheduler (this will not return until shutdown)
  k_scheduler_run();

  // Cleanup all remaining processes
  k_kill_all_processes();

  // Cleanup scheduler queues
  k_scheduler_cleanup();

  // Unmount filesystem on exit (though k_scheduler_run usually doesn't return)
  if (unmount() != FS_SUCCESS) {
    fprintf(stderr, "Warning: unmount failed on exit\n");
  }

  return 0;
}