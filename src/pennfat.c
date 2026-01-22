#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "./util/parser.h"
#include "./util/struct.h"
#include "fat_kernel.h"

#define MAX_LENGTH_READ 4096

//////////////////////////////////////////////////////////////////////////////
// =================== Helper Function Declarations =================== /////
////////////////////////////////////////////////////////////////////////////

/*!
 * @brief Read a command line from standard input.
 *
 * Reads up to 'max' bytes from STDIN into the buffer cmd[]. The input is
 * then added a trailing '\0' before returning.
 *
 * @param cmd Character buffer to store the input line. Built to have length
 *            maximum bytes by default.
 * @return Returns the number of bytes read (excluding the terminating null
 *         character) on success. Returns -1 if the read was interrupted or
 *         if input was too long.
 */
static int read_cmd(char cmd[]);

//////////////////////////////////////////////////////////////////////////////
// =================== Main function Implementation =================== /////
////////////////////////////////////////////////////////////////////////////

void reprompt(int signo) {
  write(STDERR_FILENO, "\npennfat# ", 10);
}

int main(int argc, char* argv[]) {
  // register signal handlers
  signal(SIGINT, reprompt);
  signal(SIGTSTP, reprompt);
  signal(SIGQUIT, reprompt);

  char cmd[MAX_LENGTH_READ];
  char log_buf[64];  // 64 bytes is enough for most error messages

  while (1) {
    // prompt, read command, parse command
    k_write(STDERR_FILENO, "pennfat# ", 9);

    int num_read = read_cmd(cmd);
    if (!num_read) {
      break;
    }
    if (num_read == -1) {
      const char* msg = "error reading command\n";
      k_write(STDERR_FILENO, msg, strlen(msg));
      continue;
    }
    struct parsed_command* parsed_cmd;
    if (parse_command(cmd, &parsed_cmd) < 0) {
      const char* msg = "error parsing command\n";
      k_write(STDERR_FILENO, msg, strlen(msg));
      continue;
    }
    if (parsed_cmd->num_commands == 0) {
      free(parsed_cmd);
      continue;
    }

    ////////////////////////////////////////////////////////////
    //  execute
    ////////////////////////////////////////////////////////////
    char** args = parsed_cmd->commands[0];
    if (strcmp(args[0], "mkfs") == 0) {  // mkfs
      if (!args[1] || !args[2] || !args[3]) {
        const char* msg = "mkfs: invalid arguments\n";
        k_write(STDERR_FILENO, msg, strlen(msg));
      } else {
        if (mkfs(args[1], (int)strtol(args[2], NULL, 10),
                 (int)strtol(args[3], NULL, 10)) == -1) {
          // error handled by mkfs
        }
      }
    } else if (strcmp(args[0], "mount") == 0) {  // mount
      if (!args[1]) {
        const char* msg = "mount: invalid arguments\n";
        k_write(STDERR_FILENO, msg, strlen(msg));
      } else {
        if (mount(args[1]) == -1) {
          // error handled by mount
        }
      }
    } else if (strcmp(args[0], "unmount") == 0) {  // unmount
      if (args[1]) {
        const char* msg = "unmount: invalid arguments\n";
        k_write(STDERR_FILENO, msg, strlen(msg));
      } else {
        if (unmount() == -1) {
          // error handled by unmount
        }
      }
    } else if (strcmp(args[0], "ls") == 0) {  // ls
      if (k_ls(NULL) == -1) {
        f_perror("ls");
      }
    } else if (strcmp(args[0], "touch") == 0) {  // touch
      if (!args[1]) {
        const char* msg = "touch: invalid arguments\n";
        k_write(STDERR_FILENO, msg, strlen(msg));
      } else {
        int i = 1;
        while (args[i]) {
          int fd = k_open(args[i], F_APPEND);
          if (fd == -1) {
            f_perror("touch");
          } else {
            if (k_close(fd) == -1) {
              f_perror("touch (close)");
            }
          }
          i++;
        }
      }
    } else if (strcmp(args[0], "cat") == 0) {  // cat
      if (k_cat(args) == -1) {
        f_perror("cat");
      }
    } else if (strcmp(args[0], "chmod") == 0) {  // chmod
      if (!args[1] || !args[2]) {
        const char* msg1 = "chmod: invalid arguments\n";
        k_write(STDERR_FILENO, msg1, strlen(msg1));
        const char* msg2 = "Usage: chmod PERMS FILE\n";
        k_write(STDERR_FILENO, msg2, strlen(msg2));
      } else {
        char* perm_str = args[1];
        char* filename = args[2];

        long new_perm_val = strtol(perm_str, NULL, 10);

        // Basic validation (PennFAT permissions are 0-7)
        if (new_perm_val < 0 || new_perm_val > 7) {
          const char* msg = "chmod: invalid permission value\n";
          k_write(STDERR_FILENO, msg, strlen(msg));
        } else {
          if (k_chmod_update(filename, (uint8_t)new_perm_val) == -1) {
            f_perror("chmod");
          }
        }
      }
    } else if (strcmp(args[0], "rm") == 0) {  // rm
      if (!args[1]) {
        const char* msg = "rm: invalid arguments\n";
        k_write(STDERR_FILENO, msg, strlen(msg));
      } else {
        int i = 1;

        while (args[i]) {
          const char* filename = args[i];
          if (k_unlink(filename) == -1) {
            int len = snprintf(log_buf, sizeof(log_buf),
                               "rm: error removing '%s': ", filename);
            k_write(STDERR_FILENO, log_buf, len);
            f_perror(NULL);
          }
          i++;
        }
      }
    } else if (strcmp(args[0], "mv") == 0) {  // mv
      if (!args[1] || !args[2]) {
        const char* msg = "mv: invalid arguments\n";
        k_write(STDERR_FILENO, msg, strlen(msg));
      } else {
        const char* source_filename = args[1];
        const char* dest_filename = args[2];

        if (k_mv(source_filename, dest_filename) == -1) {
          f_perror("mv");
        }
      }
    } else if (strcmp(args[0], "cp") == 0) {  // cp
      if (!args[1] || !args[2]) {
        const char* msg = "cp: invalid arguments\n";
        k_write(STDERR_FILENO, msg, strlen(msg));
      } else {
        if (k_cp(args) == -1) {
          f_perror("cp");
        }
      }
    } else {
      int len = snprintf(log_buf, sizeof(log_buf), "command not found: %s\n",
                         args[0]);
      k_write(STDERR_FILENO, log_buf, len);
    }
    free(parsed_cmd);
  }
  if (IS_FS_MOUNTED) {
    unmount();
  }
  return EXIT_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////
// =================== Static Function Implementations =================== //
////////////////////////////////////////////////////////////////////////////

static int read_cmd(char cmd[]) {
  ssize_t numBytes = read(STDIN_FILENO, cmd, MAX_LENGTH_READ);
  if (numBytes == -1) {  // should not happen.
    perror("Command line input");
    return -1;
  }

  if (numBytes == 0) {
    // This is when the input is empty and user typed Ctrl D.
    k_write(STDERR_FILENO, "\n", 1);
    return 0;
  }
  if (numBytes == MAX_LENGTH_READ && cmd[MAX_LENGTH_READ - 1] != '\n') {
    // This is when the input is factually too long. Adding '\0' here would
    // result in undesirably replacing the last char.
    const char* msg1 = "Command line input: Input too long.\n";
    k_write(STDERR_FILENO, msg1, strlen(msg1));
    char buf;
    while (read(STDIN_FILENO, &buf, 1) && buf != '\n') {
      // This while loop keeps running as long as something's still in stdin
      // and stops when it reached the last character ('\n'). In short, it
      // flushes out the remaining input left in the stdin channel.
    }
    const char* msg2 = "Done clearing input.\n";
    k_write(STDERR_FILENO, msg2, strlen(msg2));
    return -1;
  }
  if (cmd[numBytes - 1] != '\n') {
    // This is when the input is delivered by Ctrl D rather than Enter. We
    // need to actively prompt a new line and add a '\0'.
    k_write(STDERR_FILENO, "\n", 1);
    cmd[numBytes] = '\0';
  } else {
    // This is when a valid input is delivered by Enter. In this case, the
    // last character is always '\n'. Replace it with '\0'.
    cmd[numBytes - 1] = '\0';
  }

  return (int)numBytes;
}