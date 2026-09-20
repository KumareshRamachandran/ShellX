#ifndef REDIRECTION_HPP
#define REDIRECTION_HPP

#include "shellx.hpp"

// Apply file redirections for a command. Called in the child process,
// between fork() and exec(). Uses open()/dup2()/close().
// Returns true on success, false on failure (caller should _exit(1)).
bool applyRedirections(const Command& cmd);

#endif // REDIRECTION_HPP
