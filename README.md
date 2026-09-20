# ShellX

A Unix shell built from scratch using raw POSIX syscalls — `fork()`, `execvp()`, `waitpid()`, `pipe()`, `dup2()`, `sigaction()` — in C++20 on Linux. This is a hands-on systems programming exercise, not a Bash clone.

## Features

- **External commands** via `fork()`/`execvp()`/`waitpid()`
- **Built-in commands:** `cd`, `pwd`, `exit`, `jobs`
- **I/O redirection:** `>` (truncate), `>>` (append), `<` (input)
- **Pipelines:** multi-stage pipes (`cmd1 | cmd2 | cmd3`)
- **Background execution:** `cmd &` for single commands
- **Signal handling:** `SIGCHLD` (flag-only, no reaping in handler), `SIGINT` (shell survives Ctrl+C)
- **Quoted strings:** basic double-quoted string support
- **Error handling:** clear messages for invalid commands, missing files, unsupported syntax

## Architecture

```
User Input
    |
    v
Lexer / Parser  (tokens, quotes, |  <  >  >>  &)
    |
    v
Command Representation (struct Command / struct Pipeline)
    |
    +----------------------------+
    |                            |
 Built-in                  Pipeline (1+ commands)
 (cd, pwd, exit)                |
    |                     N == 1?
 run in parent               |
 (no fork)             +-----+------+
                       |            |
                  Single cmd    Multi-cmd pipeline
                       |            |
                    fork()       N-1 pipes + N forks
                       |            |
                 +-----+-----+   each child: dup2() + close + exec
                 |           |     parent: close all pipe fds + waitpid all
              child       parent
                 |           |
          [redirect]     waitpid() or track bg
                 |
              exec()
                 |
        (fail: perror + _exit())
```

### File Layout

```
ShellX/
├── CMakeLists.txt          # Build system (C++20, -Wall -Wextra -Wpedantic)
├── include/
│   ├── shellx.hpp          # Common types (Command, Pipeline), constants
│   ├── parser.hpp          # Tokenizer + command representation
│   ├── executor.hpp        # Fork/exec/wait orchestration
│   ├── builtins.hpp        # cd, pwd, exit, jobs — parent-only
│   ├── redirection.hpp     # >, >>, < file descriptor remapping
│   ├── pipeline.hpp        # Multi-stage pipe creation + fd management
│   ├── signals.hpp         # SIGCHLD + SIGINT handler installation
│   └── jobs.hpp            # Background job bookkeeping
├── src/
│   ├── main.cpp            # REPL entry point
│   ├── parser.cpp          # Tokenizer implementation
│   ├── executor.cpp        # Command execution logic
│   ├── builtins.cpp        # Built-in command handlers
│   ├── redirection.cpp     # I/O redirection via open()/dup2()
│   ├── pipeline.cpp        # Pipe creation, child wiring, fd cleanup
│   ├── signals.cpp         # Signal handler setup
│   └── jobs.cpp            # Job list management
└── tests/
    └── test_shellx.sh      # Automated test script
```

## Build & Run

### Requirements
- g++ 13+ (C++20 support)
- CMake 3.20+
- Linux

### Build

```bash
# Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Debug build with sanitizers
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build-debug
```

### Run

```bash
./build/shellx
```

### Test

```bash
bash tests/test_shellx.sh ./build/shellx
```

## Supported Grammar

```
command arg1 arg2 ...              # external command
cd [dir]                           # change directory (no args = $HOME)
pwd                                # print working directory
exit                               # exit shell (SIGTERM to bg jobs)
jobs                               # list background jobs

command > file                     # redirect stdout (truncate)
command >> file                    # redirect stdout (append)
command < file                     # redirect stdin

cmd1 | cmd2 | cmd3                 # pipeline (N commands, N-1 pipes)
command &                          # run single command in background
"hello world"                      # double-quoted string = single token
```

### Explicitly Unsupported (rejected with error message)
- `;` (command chaining)
- `&&`, `||` (logical operators)
- `$(...)`, `` `...` `` (command substitution)
- `cmd1 | cmd2 &` (backgrounded pipelines)

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Child termination | `_exit()` after failed `exec()` | Prevents double-flushing of inherited stdio buffers |
| Exec failure code | `ENOENT` → 127, `EACCES` → 126 | Standard shell convention for "not found" vs "not executable" |
| Signal installation | `sigaction()` with `SA_RESTART` | Reliable semantics; handler stays installed |
| SIGCHLD handler | Flag-only (`sig_atomic_t`) | Eliminates race where handler and foreground `waitpid()` both try to reap the same child |
| Reaping ownership | Foreground: executor's blocking `waitpid()`. Background: `reapFinishedJobs()` in REPL | Exactly one `waitpid()` call site per PID — no race |
| Pipeline exit status | Last command's status | Matches bash's default (non-`pipefail`) behavior |
| SIGINT strategy | Shell ignores; child restores `SIG_DFL` | Shell survives Ctrl+C; children respond normally |
| Exit behavior | `SIGTERM` to bg jobs, no reap | Shell exits immediately; orphans cleaned up by init |
| Fd discipline | Every process closes every fd it doesn't own | Single rule preventing all pipeline hangs and fd leaks |

## Known Limitations

- **Background jobs are not isolated from Ctrl+C.** Process groups are not implemented in this version. Without `setpgid()` separating foreground and background jobs into distinct groups, a background child stays in the shell's foreground process group, so a terminal-generated `SIGINT` (Ctrl+C) can reach it too. This is a known gap — not an oversight.

- **Backgrounded pipelines (`cmd1 | cmd2 &`) are not supported.** The `Job` struct tracks a single PID; a backgrounded pipeline needs multi-PID job tracking, which is deferred.

- **No process group control.** Both limitations above are related — proper job control needs process groups. Deferred until the core shell is stable.

## Debugging

Useful tools for investigating ShellX behavior:

```bash
# Trace syscalls
strace ./build/shellx

# Check for zombies
ps aux | grep shellx
pstree -p $$

# Check for fd leaks
lsof -p <shellx-pid>

# Debug with gdb
gdb ./build-debug/shellx
```
