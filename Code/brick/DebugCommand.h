#pragma once
#include <stddef.h>
#include <string.h>

enum class DebugCommand { None, Enable, Disable, Probe, Reinitialize, Unknown };

// Bounded, nonblocking line parser: no dynamic allocation or partial commands.
class DebugCommandParser {
  char line[32] = {};
  size_t length = 0;
  bool overflow = false;
public:
  DebugCommand feed(char value) {
    if (value == '\r' || value == '\n') {
      if (!length && !overflow) return DebugCommand::None;
      DebugCommand command = DebugCommand::Unknown;
      if (!overflow) {
        line[length] = 0;
        if (!strcmp(line, "--debug")) command = DebugCommand::Enable;
        else if (!strcmp(line, "--no-debug") || !strcmp(line, "--debug-off")) command = DebugCommand::Disable;
        else if (!strcmp(line, "--debug-i2c")) command = DebugCommand::Probe;
        else if (!strcmp(line, "--debug-reinit")) command = DebugCommand::Reinitialize;
      }
      length = 0; overflow = false;
      return command;
    }
    if (!overflow) {
      if (length < sizeof(line)-1) line[length++] = value;
      else overflow = true;
    }
    return DebugCommand::None;
  }
};
