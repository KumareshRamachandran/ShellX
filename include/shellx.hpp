#ifndef SHELLX_HPP
#define SHELLX_HPP

#include <string>
#include <vector>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// --- Resource limits ---
constexpr int MAX_PIPELINE_LENGTH = 16;
constexpr size_t MAX_INPUT_LENGTH = 4096;
constexpr int MAX_BACKGROUND_JOBS = 64;

// --- Command representation ---

struct Command {
    std::vector<std::string> args;  // argv[0] = program, rest = arguments
    std::string input_file;         // "" if none, filename if '<'
    std::string output_file;        // "" if none, filename if '>' or '>>'
    bool append_mode = false;       // true for '>>', false for '>'
    // NOTE: no background flag here — '&' applies to the whole pipeline, not
    // a single command, in this grammar. Pipeline::background is the single
    // source of truth; keeping a second copy here would allow inconsistent state.
};

struct Pipeline {
    std::vector<Command> commands;  // 1+ commands chained by '|'
    bool background = false;        // trailing '&' on the whole pipeline — sole source of truth
};

#endif // SHELLX_HPP
