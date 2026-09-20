# ShellX — Implementation Plan
### Unix Shell & Process Manager (C++20, Linux, POSIX)

> **Goal:** Build a fully functional Unix shell from scratch using raw POSIX syscalls — `fork()`, `execvp()`, `waitpid()`, `pipe()`, `dup2()`, `sigaction()` — with correct fd lifecycle management, async-signal-safe `SIGCHLD` notification, and proper `SIGINT` handling. The project is a hands-on systems programming exercise, not a Bash clone.

---

## Final Contract (locked — do not revise without cause)

This section is the single source of truth for the four decisions finalized during design review. Every other section in this document has been updated to match it; if anything below conflicts with wording elsewhere in the plan, **this section wins.**

```text
SIGCHLD  → handler sets a sig_atomic_t flag only; normal code performs waitpid() + job updates
Pipeline → shell's reported exit status = status of the LAST command in the pipeline
exec()   → ENOENT → _exit(127)   (command not found)
           EACCES → _exit(126)   (found but not executable)
Shutdown → send SIGTERM to remaining background jobs on exit; no explicit reap;
           documented as intentional (shell is terminating anyway)
Order    → core shell (fork/exec/wait, redirection, pipes, single-command background,
           signals) must be fully stable before any process-group work begins
```

**Explicitly deferred (not in this build):** multi-PID job tracking for backgrounded pipelines (`cmd1 | cmd2 &`) and process-group-based Ctrl+C isolation for background jobs. Both are real gaps identified in review, but per the Order rule above they're out of scope until the core shell is stable. Current scope backgrounds **single commands only** (`sleep 3 &`); a backgrounded pipeline is not a supported construct in this pass. Revisit both together in a later phase if time allows — they're related (proper job control needs both).

---

## Build Environment

| Tool | Version |
|---|---|
| Compiler | g++ 13.3.0 (C++20 support ✓) |
| Build system | CMake 3.28.3 |
| OS | Ubuntu 24.04, Linux 7.0.0, x86_64 |

---

## Proposed Changes

### Repository Layout

```text
ShellX/
├── CMakeLists.txt                  # [NEW] Build system with Debug/Release + sanitizer support
├── README.md                       # [MODIFY] Full project narrative + debugging notes
├── docs/                           # Existing — untouched (reference guides)
├── include/
│   ├── shellx.hpp                  # [NEW] Common types, constants, forward declarations
│   ├── parser.hpp                  # [NEW] Tokenizer + command representation
│   ├── executor.hpp                # [NEW] Fork/exec/wait orchestration
│   ├── builtins.hpp                # [NEW] cd, pwd, exit — parent-only commands
│   ├── redirection.hpp             # [NEW] >, >>, < file descriptor remapping
│   ├── pipeline.hpp                # [NEW] Multi-stage pipe creation + fd management
│   ├── signals.hpp                 # [NEW] SIGCHLD + SIGINT handler installation
│   └── jobs.hpp                    # [NEW] Background job bookkeeping
├── src/
│   ├── main.cpp                    # [NEW] REPL entry point
│   ├── parser.cpp                  # [NEW] Tokenizer implementation
│   ├── executor.cpp                # [NEW] Command execution logic
│   ├── builtins.cpp                # [NEW] Built-in command handlers
│   ├── redirection.cpp             # [NEW] I/O redirection via open()/dup2()
│   ├── pipeline.cpp                # [NEW] Pipe creation, child wiring, fd cleanup
│   ├── signals.cpp                 # [NEW] Signal handler setup (SIGCHLD/SIGINT — notification only)
│   └── jobs.cpp                    # [NEW] Job list management
└── tests/
    └── test_shellx.sh              # [NEW] Automated shell test script
```

---

## Architecture

```text
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

---

## Core Data Structures

### Command Representation

```cpp
// include/shellx.hpp

#include <string>
#include <vector>

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
```

### Job Tracking

```cpp
// include/jobs.hpp

#include <string>
#include <vector>
#include <sys/types.h>

struct Job {
    pid_t pid;
    std::string command;
    bool running;
};

// Global job list — accessed from normal (non-signal) code only: main loop
// and reapFinishedJobs(). The SIGCHLD handler never touches this vector —
// it only sets g_sigchld_pending; all Job updates happen afterward, in the
// main loop.
```

---

## Phased Implementation

### Phase 1 — Skeleton + REPL + Signal Setup
**Files:** `main.cpp`, `shellx.hpp`, `signals.hpp`, `signals.cpp`, `CMakeLists.txt`

#### [NEW] [`CMakeLists.txt`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/CMakeLists.txt)

```cmake
cmake_minimum_required(VERSION 3.20)
project(ShellX LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(shellx
    src/main.cpp
    src/parser.cpp
    src/executor.cpp
    src/builtins.cpp
    src/redirection.cpp
    src/pipeline.cpp
    src/signals.cpp
    src/jobs.cpp
)
target_include_directories(shellx PRIVATE include)
target_compile_options(shellx PRIVATE -Wall -Wextra -Wpedantic)
```

#### [NEW] [`shellx.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/shellx.hpp)
- `Command` and `Pipeline` structs (as above)
- Common includes (`unistd.h`, `sys/wait.h`, `fcntl.h`, `signal.h`, etc.)
- Constants: `MAX_PIPELINE_LENGTH = 16`, `MAX_INPUT_LENGTH = 4096`, `MAX_BACKGROUND_JOBS = 64`

#### [NEW] [`signals.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/signals.hpp) / [`signals.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/signals.cpp)
- `installSignalHandlers()` — called **once at shell startup, before the REPL loop**
- `SIGCHLD` handler — **notification only, no reaping happens here:**
  - Sets `volatile sig_atomic_t g_sigchld_pending = 1` and returns immediately
  - Does **not** call `waitpid()` — all reaping happens later, in normal (non-signal) code, so there is exactly one place that ever calls `waitpid()` on any given PID
  - **Does NOT call `printf`, `malloc`, `new`, STL ops** — async-signal-safety
- `SIGINT` handler:
  - Does NOT kill the shell
  - Sets a `volatile sig_atomic_t g_sigint_received` flag
  - Shell checks flag in REPL to re-print prompt if no foreground child
- Both installed via `sigaction()` (not `signal()`)

#### [NEW] [`main.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/main.cpp)
- Install signal handlers
- REPL: `while (true) { print_prompt() → getline() → parse() → execute() }`
- After each iteration, check `g_sigchld_pending` and update job list
- Handle empty input gracefully (just re-prompt)
- **Enforce `MAX_INPUT_LENGTH` explicitly** after `getline()` returns — `getline()` itself has no length cap, so check `line.size()` and reject/truncate with an error rather than assuming the constant does anything on its own

> [!IMPORTANT]
> Signal handlers **must** be installed before the REPL loop starts and before any `fork()` call. A background child finishing before handler installation would miss `SIGCHLD`, leaving a zombie.

---

### Phase 2 — Parser
**Files:** `parser.hpp`, `parser.cpp`

#### [NEW] [`parser.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/parser.hpp) / [`parser.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/parser.cpp)
- `Pipeline parse(const std::string& line)` — main entry point
- **Tokenization rules:**
  1. Split on whitespace
  2. Recognize special tokens: `|`, `<`, `>`, `>>`, `&`
  3. Handle basic double-quoted strings (`"hello world"` → single token)
  4. Strip `&` from end → set `Pipeline::background = true`
- **Redirection parsing:**
  - `>` / `>>` / `<` followed by a filename → populate `Command::output_file` / `input_file`
  - Remove redirection tokens + filename from `args`
- **Pipe splitting:**
  - Split token stream on `|` into multiple `Command` entries in the `Pipeline`
- **Grammar boundary:** clearly reject unsupported constructs (`;`, `&&`, `||`, `$(...)`, backticks) with an error message rather than silently mishandling them
- **Resource limits:** reject pipelines with > `MAX_PIPELINE_LENGTH` commands

---

### Phase 3 — Built-ins (Parent-Only)
**Files:** `builtins.hpp`, `builtins.cpp`

#### [NEW] [`builtins.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/builtins.hpp) / [`builtins.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/builtins.cpp)
- `bool isBuiltin(const std::string& cmd)` — check if command is `cd`, `pwd`, or `exit`
- `int executeBuiltin(const Command& cmd)` — dispatch to the right handler
- **`cd` implementation:**
  - `chdir(args[1].c_str())` — directly in parent, **never fork**
  - No args → `chdir(getenv("HOME"))` (go to home directory)
  - Error handling via `perror("cd")`
- **`pwd` implementation:**
  - `getcwd()` → print to stdout
- **`exit` implementation:**
  - Call `killAllJobs()` — send `SIGTERM` to all remaining background jobs, no explicit reap (locked behavior, see Final Contract)
  - Return a sentinel value that breaks the REPL loop

> [!IMPORTANT]
> Built-in check must happen **before** any fork decision. The executor checks `isBuiltin()` first — if true, runs in-process and returns immediately.

---

### Phase 4 — Single Command Execution (fork/exec/wait)
**Files:** `executor.hpp`, `executor.cpp`

#### [NEW] [`executor.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/executor.hpp) / [`executor.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/executor.cpp)
- `int executePipeline(const Pipeline& pipeline)` — main dispatch:
  1. If single command + builtin → `executeBuiltin()`
  2. If single command → `executeSingle()`
  3. If multi-command → `executePipeline()` (Phase 6)
- `int executeSingle(const Command& cmd, bool background)`:
  - `pid_t pid = fork();`
  - **`pid == -1`:** `perror("fork")`, return error, continue REPL
  - **`pid == 0` (child):**
    1. Apply redirections (Phase 5)
    2. Build `argv` as `const char*[]` for `execvp`
    3. `execvp(argv[0], argv)`
    4. If `execvp` returns → `perror(argv[0])` → **`_exit()`** (NOT `exit()`), with the code chosen by `errno`:
       - `errno == ENOENT` → `_exit(127)` — command not found
       - `errno == EACCES` → `_exit(126)` — found but not executable (e.g. permissions, or it's a directory)
       - anything else → `_exit(126)` as a reasonable default
  - **`pid > 0` (parent):**
    - Foreground: `waitpid(pid, &status, 0)`, retried explicitly on `EINTR` (see note below) → inspect `WIFEXITED`/`WEXITSTATUS`/`WIFSIGNALED`/`WTERMSIG`
    - Background: add to job list, print `[job_id] pid`, return immediately

> [!WARNING]
> **`_exit()` not `exit()` in the child after failed `exec()`.** `exit()` flushes stdio buffers and runs `atexit` handlers — the forked child may have inherited buffered output from the parent, causing it to be flushed twice (duplicated output). `_exit()` skips all cleanup and terminates immediately.
>
> **Exit code is a chosen convention, not a universal mapping.** `127`/`126` are the standard shell conventions for "not found" vs. "not executable" respectively — check `errno` after the failed `execvp()` rather than hardcoding `127` for every failure mode.

> [!NOTE]
> **`EINTR` around the foreground `waitpid()`.** With `sigaction()`'s `SA_RESTART` flag, most interrupted syscalls resume automatically — but don't rely on that alone to explain the shell's behavior; make the retry explicit in code so it's documented rather than implicit:
> ```cpp
> pid_t result;
> do {
>     result = waitpid(pid, &status, 0);
> } while (result == -1 && errno == EINTR);
> ```
> This matters here specifically because a background child finishing (and delivering its own `SIGCHLD`) while the shell is blocked in a *different*, foreground `waitpid()` call is a real, expected occurrence — not an edge case. The retry loop makes that interruption harmless instead of leaving it to `SA_RESTART` to paper over silently.

---

### Phase 5 — I/O Redirection
**Files:** `redirection.hpp`, `redirection.cpp`

#### [NEW] [`redirection.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/redirection.hpp) / [`redirection.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/redirection.cpp)
- `bool applyRedirections(const Command& cmd)` — called **in the child, before `exec()`**
- **Output redirection (`>`):**
  - `int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644)`
  - `dup2(fd, STDOUT_FILENO)`
  - `close(fd)` — original fd no longer needed after dup2
- **Append redirection (`>>`):**
  - `int fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0644)`
  - Same dup2 + close pattern
- **Input redirection (`<`):**
  - `int fd = open(file, O_RDONLY)`
  - `dup2(fd, STDIN_FILENO)`
  - `close(fd)`
- All `open()` calls checked for `-1` → `perror()` + return false → child calls `_exit(1)`

---

### Phase 6 — Pipelines (Hardest Phase — Budget Extra Time)
**Files:** `pipeline.hpp`, `pipeline.cpp`

#### [NEW] [`pipeline.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/pipeline.hpp) / [`pipeline.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/pipeline.cpp)
- `int executePipelineChain(const Pipeline& pipeline)` — N commands, N-1 pipes

**Algorithm for N-command pipeline:**

```text
For N commands (indexed 0..N-1):
  Create N-1 pipes: pipes[0], pipes[1], ..., pipes[N-2]
    Each pipe: int fds[2]; pipe(fds);

  For each command i (0..N-1):
    fork() → child:
      // Wire stdin from previous pipe (if not first command)
      if (i > 0):
        dup2(pipes[i-1][0], STDIN_FILENO)

      // Wire stdout to next pipe (if not last command)
      if (i < N-1):
        dup2(pipes[i][1], STDOUT_FILENO)

      // Close ALL pipe fds in this child (both ends of every pipe)
      for each pipe in pipes:
        close(pipe[0])
        close(pipe[1])

      // Apply any file redirections (> < >>)
      applyRedirections(cmd)

      // exec
      execvp(...)
      // if exec fails: perror + _exit(errno == ENOENT ? 127 : 126)

  // Parent: close ALL pipe fds
  for each pipe in pipes:
    close(pipe[0])
    close(pipe[1])

  // Wait for all children (parent owns this wait — the SIGCHLD handler
  // never calls waitpid() itself, see Final Contract). Retry on EINTR,
  // same as the single-command foreground path (Phase 4).
  for each child pid (in order):
    do { waitpid(pid, &status, 0) } while (result == -1 && errno == EINTR)
    if this is the LAST command in the pipeline:
      pipeline_status = status   // shell reports the last command's status
```

> [!CAUTION]
> **The #1 source of bugs:** If ANY process (parent or child) keeps an unused pipe fd open, the reader never sees EOF and hangs forever. The rule is simple: **every process closes every fd it doesn't own.** After `dup2()`, close the original. In every child, close ALL pipe fds (not just "your" pipe). In the parent, close ALL pipe fds immediately after all forks.

> [!NOTE]
> **Pipeline exit status convention:** the shell reports the exit status of the **last** command in the pipeline (matches bash's default, non-`pipefail` behavior). E.g. `false | true` reports success. This is a deliberate, documented simplification — not a bug.

---

### Phase 7 — Background Jobs + SIGCHLD Reaping
**Files:** `jobs.hpp`, `jobs.cpp` (+ signal handler from Phase 1)

#### [NEW] [`jobs.hpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/include/jobs.hpp) / [`jobs.cpp`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/src/jobs.cpp)
- `void addJob(pid_t pid, const std::string& cmd)` — add to background job list
- `void reapFinishedJobs()` — called from REPL after `g_sigchld_pending` flag is set. **This function, not the signal handler, owns all `waitpid()` calls for background children:**
  - Loops through job list
  - For each running job, calls `waitpid(pid, &status, WNOHANG)` to check
  - If reaped → print `"[N] Done: command"`, mark as not running, record exit status
  - Remove finished jobs from list
- `void printJobs()` — optional `jobs` builtin
- `void killAllJobs()` — send `SIGTERM` to all running background jobs on shell exit. **Does not wait for them to terminate or reap them** — the shell process is exiting immediately afterward, so any resulting zombies are cleaned up when they're reparented to `init`/PID 1. This is intentional, not an oversight.

**SIGCHLD flow (handler is notification-only — see Final Contract):**
```text
Background child exits
    ↓
Kernel sends SIGCHLD to shell
    ↓
sigchld_handler(): sets g_sigchld_pending = 1, returns immediately
                    (does NOT call waitpid — no reaping in the handler)
    ↓
Main REPL loop detects g_sigchld_pending == 1
    ↓
reapFinishedJobs() calls waitpid(pid, &status, WNOHANG) per tracked job,
                    updates Job vector, prints completion messages
    ↓
Reset g_sigchld_pending = 0
```

**Why this matters:** if the handler reaped children itself via `waitpid(-1, ..., WNOHANG)`, it could race with the foreground path's blocking `waitpid(fg_pid, &status, 0)` — the handler might reap the foreground child first, and the foreground `waitpid()` call would then fail with `ECHILD`. Making the handler flag-only means `waitpid()` for any given PID is called from exactly one place: `reapFinishedJobs()` for background jobs, the foreground executor for foreground jobs. No overlap, no race.

---

### Phase 8 — Ctrl+C (SIGINT) Handling
**Integrated into:** `signals.cpp`, `executor.cpp`

**Chosen behavior:** The shell ignores `SIGINT` (stays alive). The foreground child receives `SIGINT` via the default terminal-generated signal delivery (since the child inherits the terminal's foreground process group). If no foreground child is running, the shell re-prints the prompt.

**Implementation:**
- Shell installs `SIG_IGN` or a no-op handler for `SIGINT` at startup
- Before `exec()` in the child, restore `SIGINT` to `SIG_DFL` — so the executed program responds normally to Ctrl+C
- Parent's `waitpid()` for foreground processes handles `WIFSIGNALED` + `WTERMSIG` to detect signal-killed children

> [!NOTE]
> **Known limitation — background jobs are not isolated from Ctrl+C.** Process groups are explicitly deferred (see Final Contract). Without `setpgid()` separating foreground and background jobs into distinct groups, a background child stays in the shell's own foreground process group, so a terminal-generated `SIGINT` (Ctrl+C) can reach it too, not just the intended foreground child. This is a real, known gap in this version — not an oversight — and must be stated explicitly in the README (see Phase 9) rather than left implicit.

---

### Phase 9 — Polish + Testing
**Files:** `tests/test_shellx.sh`, `README.md`

#### [NEW] [`tests/test_shellx.sh`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/tests/test_shellx.sh)

Automated test script exercising the full testing matrix:

| Category | Test Cases |
|---|---|
| Basic commands | `pwd`, `ls`, `echo hello` |
| Built-ins | `cd ..` + `pwd` verification, `cd` with no args, `exit` |
| Redirection | `echo hello > a.txt`, `cat < a.txt`, `echo world >> a.txt`, verify contents |
| Pipes | `ls \| grep cpp`, `cat a.txt \| sort`, `cat a.txt \| sort \| uniq` (multi-stage) |
| Background | `sleep 1 &` — verify prompt returns, no zombie after completion |
| Signals | `sleep 20` + Ctrl+C — shell survives |
| Signals (known limitation) | `sleep 20 &` then Ctrl+C — document observed behavior (background job may also receive `SIGINT`, since process groups aren't implemented) |
| Failure cases | nonexistent command, missing file for `<`, broken pipe syntax |
| Exit behavior | Start background job → `exit` → verify chosen behavior |

#### [MODIFY] [`README.md`](file:///home/kumaresh/Desktop/Dev/Personal/ShellX/README.md)
- Project overview + architecture diagram
- Build + run instructions
- Supported grammar (explicitly documented scope)
- Design decisions (exit behavior, SIGINT strategy, fd discipline)
- **Explicit known-limitations note:** *"Until process groups are implemented, background jobs do not have independent terminal signal isolation — Ctrl+C may reach a background job as well as the intended foreground job."*
- Debugging notes section (real bugs encountered)
- Testing instructions

---

## Execution Order

| Step | What | Depends On | Files |
|---|---|---|---|
| 1 | CMakeLists.txt + skeleton headers (all `.hpp` with empty stubs) | — | CMakeLists.txt, all include/*.hpp |
| 2 | Signal handler setup | Step 1 | signals.hpp/cpp |
| 3 | REPL loop (main.cpp) | Steps 1, 2 | main.cpp |
| 4 | Parser (tokenizer, pipe/redirect/bg detection) | Step 1 | parser.hpp/cpp |
| 5 | Built-ins (cd, pwd, exit) | Steps 3, 4 | builtins.hpp/cpp |
| 6 | Single command execution (fork/exec/wait) | Steps 3, 4, 5 | executor.hpp/cpp |
| 7 | I/O redirection (>, >>, <) | Step 6 | redirection.hpp/cpp |
| 8 | Two-stage pipe | Steps 6, 7 | pipeline.hpp/cpp |
| 9 | Multi-stage pipe generalization | Step 8 | pipeline.cpp |
| 10 | Background execution (&) | Steps 6, 2 | executor.cpp, jobs.hpp/cpp |
| 11 | SIGCHLD reaping integration | Steps 2, 10 | signals.cpp, jobs.cpp |
| 12 | Ctrl+C handling refinement | Steps 2, 6 | signals.cpp, executor.cpp |
| 13 | Test script + README | Steps 1–12 | tests/test_shellx.sh, README.md |

### Day Checkpoints

| Day | Target | Steps | Milestone |
|---|---|---|---|
| **Day 1** | Build system + REPL + parser + built-ins | 1–5 | `ls`, `echo hello`, `cd ..`, `pwd` work interactively |
| **Day 2** | Single command exec + redirections | 6–7 | `ls > out.txt`, `cat < out.txt`, `echo world >> out.txt` work |
| **Day 3** | Pipes (2-stage + multi-stage) | 8–9 | `ls \| wc -l`, `cat file \| sort \| uniq` with no hangs |
| **Day 4** | Background jobs + SIGCHLD + Ctrl+C | 10–12 | `sleep 3 &` doesn't block; Ctrl+C doesn't kill shell |
| **Day 5** | Testing + README + polish | 13 | Full test matrix passes, clean README |

---

## Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Language | C++20 | RAII for fd cleanup, `std::string`/`std::vector` for ergonomics, familiar for the author |
| Child termination | `_exit()` after failed `exec()` | Prevents double-flushing of inherited stdio buffers |
| Exec failure code | `errno == ENOENT` → `127`; `errno == EACCES` → `126` | Matches standard shell convention for "not found" vs. "not executable"; a chosen convention, not a universal `exec()` mapping |
| Signal installation | `sigaction()` | Reliable semantics (handler stays installed), SA_RESTART for interrupted syscalls |
| **SIGCHLD handler** | **Flag-only — sets `sig_atomic_t`, never calls `waitpid()`** | **Locked decision.** Keeps the handler trivially async-signal-safe and eliminates the race where the handler and the foreground `waitpid()` both try to reap the same child, which would surface as a spurious `ECHILD` |
| Reaping ownership | Foreground: executor's blocking `waitpid()`. Background: `reapFinishedJobs()` in the main loop, driven by the flag | Exactly one call site ever calls `waitpid()` on a given PID — no shared responsibility, no race |
| Pipeline exit status | Status of the **last** command in the pipeline | Matches bash's default (non-`pipefail`) behavior; simple, explainable convention |
| SIGINT strategy | Shell ignores; child restores `SIG_DFL` | Shell survives Ctrl+C; children respond normally |
| Exit-time behavior | Kill remaining background jobs via `SIGTERM`; **no explicit reap** | Shell exits immediately afterward, so reaping is unnecessary — orphaned/terminated children are cleaned up by `init`; documented as intentional, not incomplete |
| Fd discipline | Every process closes every fd it doesn't own | Single rule that prevents all pipeline hangs and fd leaks |
| Parser scope | Whitespace + `\|` + `>` `>>` `<` + `&` + basic quotes | Explicitly reject `&&`, `\|\|`, `;`, `$()` — credible subset, not a fake Bash |
| Resource limits | Max 16 pipeline stages, 64 bg jobs, 4096-char input | Prevents resource exhaustion without adding complexity; enforce input length explicitly after `getline()`, since `getline()` alone doesn't cap it |
| Build config | `-Wall -Wextra -Wpedantic` + sanitizer support | Catch bugs early; ASan/UBSan available via cmake flags |
| Background scope | Single commands only (`cmd &`); backgrounded pipelines (`cmd1 \| cmd2 &`) **out of scope for this pass** | `Job` tracks one PID; a backgrounded pipeline needs multi-PID job tracking, deferred per the Order rule in the Final Contract |
| Process groups | **Not implemented in this pass** | Needed for fully correct Ctrl+C isolation of background jobs, but deferred until the core shell (fork/exec/wait/pipes/background/signals) is stable — see Final Contract |

---

## Design Rules

1. **Every process closes every fd it doesn't own.** This prevents pipeline hangs and fd leaks.
2. **Built-ins never fork.** Check `isBuiltin()` before any fork logic runs.
3. **`_exit()`, not `exit()`, in a child after failed `exec()`.**
4. **Signal handlers do the minimum:** set a `sig_atomic_t` flag and return — no `waitpid()`, no reaping, nothing else.
5. **`execvp()` already searches `PATH`** — don't reimplement path resolution.
6. **Check every syscall return value.** `fork()`, `exec()`, `open()`, `pipe()`, `dup2()`, `waitpid()`, `chdir()` — all can fail. Use `perror()` and handle gracefully.

---

## Verification Plan

### Automated Tests
```bash
# Debug build with sanitizers
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build-debug
./build-debug/shellx  # manual interactive testing

# Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/shellx

# Run test script
bash tests/test_shellx.sh ./build/shellx
```

### Manual Verification
- Interactive testing through the full test matrix (Section: Phase 9)
- `strace ./build/shellx` to observe real syscalls
- `ps aux | grep shellx` + `pstree` to verify no zombie accumulation
- `lsof -p <pid>` to verify no fd leaks
- Cross-check pipe/redirection output against `/bin/bash` for supported subset

### Debugging Tools
`gdb`, `strace`, `ps`, `pstree`, `lsof` — `strace` is especially useful since ShellX is fundamentally a syscall-learning project.

---

## Definition of Done

- [ ] Any external command runs via `fork`/`exec`/`wait`
- [ ] `cd`, `pwd`, `exit` work correctly as built-ins (parent process, no fork)
- [ ] `cd` with no args goes to `$HOME`
- [ ] `>`, `>>`, `<` all work correctly with proper file flags
- [ ] Two-stage pipes work with no hangs
- [ ] Multi-stage pipes (`cmd1 | cmd2 | cmd3`) work with no hangs
- [ ] Background (`&`) commands don't block the shell (single commands only — backgrounded pipelines are out of scope)
- [ ] `SIGCHLD` handler sets a flag only and never calls `waitpid()` itself
- [ ] Background processes are reaped in the main loop via `reapFinishedJobs()`, not left as zombies
- [ ] Foreground `waitpid()` never returns `ECHILD` due to a handler/foreground reaping collision
- [ ] Pipeline exit status reflects the **last** command's status
- [ ] Ctrl+C doesn't kill the shell itself
- [ ] `_exit()` used after failed `exec()` in the child, with `127` for `ENOENT` and `126` for `EACCES`
- [ ] SIGINT restored to `SIG_DFL` in child before `exec()`
- [ ] Exit-time behavior: `SIGTERM` sent to all remaining background jobs on `exit`, no explicit reap (documented as intentional)
- [ ] Invalid commands produce clear error messages
- [ ] All syscall failures checked and reported via `perror()`
- [ ] No fd leaks (verified via `lsof`)
- [ ] Input length capped at `MAX_INPUT_LENGTH`, enforced explicitly after `getline()`
- [ ] Test script passes
- [ ] README documents architecture, grammar, design decisions, and debugging notes
