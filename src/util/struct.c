#include "./struct.h"
#include <stdlib.h>

// Initialize a PCB (set all fields to default values)
void pcb_init(pcb_t* pcb) {
  if (!pcb)
    return;
  pcb->state = P_READY;
  pcb->prio = 1;  // Default priority
  pcb->wake_tick = 0;
  pcb->stopped_reported = false;
  pcb->ppid = 0;
  pcb->parent = NULL;
  pcb->exit_status = P_EXIT_NONE;  // Haven't exited yet.
  pcb->childs = vec_new(5, NULL);
  for (int i = 0; i < MAX_FD; i++) {
    pcb->fd_table[i] = -1;
  }
  pcb->cmd_name[0] = '\0';  // Empty command name
  pcb->args = NULL;
}

// Initialize an open file entry
void open_file_init(open_file_t* file) {
  if (!file)
    return;
  file->name[0] = '\0';
  file->size = 0;
  file->perm = 0;

  file->first_block = 0x0000;
  file->dirent_offset = 0;

  file->offset = 0;
  file->flag = 0;
}
