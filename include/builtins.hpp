#ifndef BUILTINS_HPP
#define BUILTINS_HPP

#include "shellx.hpp"
#include <string>

// Check if the command name is a built-in (cd, pwd, exit).
bool isBuiltin(const std::string& cmd);

// Execute a built-in command in the parent process (no fork).
// Returns the exit status:
//   0   = success
//   1   = error (e.g. cd to nonexistent dir)
//  -1   = sentinel for "exit" — caller should break the REPL loop
int executeBuiltin(const Command& cmd);

#endif // BUILTINS_HPP
