#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>
#include <stddef.h>

// Error codes
enum {
  UNEXPECTED_FILE_INPUT = -2,
  UNEXPECTED_FILE_OUTPUT = -3,
  UNEXPECTED_PIPELINE = -4,
  UNEXPECTED_AMPERSAND = -5,
  EXPECT_INPUT_FILENAME = -6,
  EXPECT_OUTPUT_FILENAME = -7,
  EXPECT_COMMANDS = -8
};

struct parsed_command {
  int is_background;
  int is_file_append;
  char* stdin_file;
  char* stdout_file;
  size_t num_commands;
  char** commands[];  // Flexible array member
};

/**
 * @brief Parse a command line string into a parsed_command structure.
 * 
 * @param line The command line string to parse
 * @param pcmd Pointer to store the parsed command (allocated by this function)
 * @return 0 on success, negative error code on error
 */
int parse_command(const char* line, struct parsed_command** pcmd);

#endif
