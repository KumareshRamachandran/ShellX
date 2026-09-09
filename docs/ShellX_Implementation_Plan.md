# ShellX — Implementation Plan
### Unix Shell & Process Manager (C/C++, Linux, POSIX)

*Synthesized from the three uploaded guides — this plan uses the most refined version of each stage (the "Updated" guide's corrections, like `_exit()` and async-signal-safety, are folded in throughout).*

**Time budget:** 5–6 days
**Builds on:** nothing prior — self-contained, no external dependencies, no networking
**Risk level:** Low-medium — new concepts, but well-documented, fully testable on your own machine

---

## 1. What You're Building

A working command-line shell — a stripped-down `bash` — that runs programs, chains them with pipes, redirects I/O to files, and manages foreground/background processes, all implemented directly against Linux/POSIX syscalls (`fork`, `exec`, `waitpid`, `pipe`, `dup2`, signals). The point isn't the shell itself — it's forced, hands-on mastery of process management, IPC, file descriptors, and signals.

---

## 2. Tech Stack

| Component | Choice |
|---|---|
| Language | C++17/20 (or plain C — either defensible; C++ gives you RAII for fd cleanup) |
| Environment | Linux native or WSL (POSIX-only — won't work natively on Windows) |
| Core headers | `unistd.h`, `sys/wait.h`, `signal.h`, `fcntl.h`, `sys/types.h` |
| Build system | CMake (or a plain Makefile) |
| Key APIs | `fork()`, `execvp()`, `waitpid()`, `pipe()`, `dup2()`, `open()`, `close()`, `chdir()`, `sigaction()`, `_exit()` |
| Testing | Manual interactive testing + a small scripted test matrix (Section 8) |

---

## 3. Repository Structure

```text
shellx/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── parser.hpp
│   ├── executor.hpp
│   ├── builtins.hpp
│   ├── redirection.hpp
│   ├── pipeline.hpp
│   ├── signals.hpp
│   └── jobs.hpp        # background-job bookkeeping (PID, command, running/finished)
├── src/
│   ├── main.cpp
│   ├── parser.cpp
│   ├── executor.cpp
│   ├── builtins.cpp
│   ├── redirection.cpp
│   ├── pipeline.cpp
│   ├── signals.cpp
│   └── jobs.cpp
└── tests/
```

---

## 4. Architecture

```text
User Input
    |
    v
Lexer / Parser  (tokens, quotes, |  <  >  >>  &)
    |
    v
Command Representation
    |
    +----------------------------+
    |                            |
 Built-in                External command
 (cd, pwd, exit)               |
    |                        fork()
 run in parent                 |
 (no fork)               +-----+-----+
                          |           |
                       child       parent
                          |           |
                       exec()     waitpid()
                          |           |
                (fail: report + _exit())  WIFEXITED / WEXITSTATUS
```

For a pipeline (`cmd1 | cmd2`): two children, connected by `pipe()`, each `dup2()`-remapped and with every unused fd end closed in both children and the parent.

---

## 5. Build Plan (Day by Day)

### Days 1–2 — Core execution loop

1. **REPL skeleton:** print prompt → read line (`fgets`/`getline`) → parse → execute → loop.
   Install your `SIGCHLD` and `SIGINT` handlers **before** this loop starts (see Day 5–6, ordering matters).
2. **Tokenizer:** split the line into `argv[]` on whitespace; handle basic quoted strings (`echo "hello world"`) if you want polish, but don't over-invest — do **not** attempt Bash-compatible parsing, it's out of scope.
3. **`fork()` + `execvp()` + `waitpid()`:**
   - `pid_t pid = fork();` → `0` in the child, child's PID in the parent, `-1` on error.
   - **Handle `-1` explicitly** — report the error and continue the REPL rather than assuming a child exists.
   - **Child (`pid == 0`):** call `execvp(argv[0], argv)`. It only returns on failure.
     - On failure, print the error, then call **`_exit()`**, not `exit()`. `exit()` flushes C++ stdio buffers and runs `atexit` handlers — if the parent had unflushed buffered output at `fork()` time, the child can flush it *again*, producing duplicated output. `_exit()`/`std::_Exit()` skips all that and terminates immediately, which is what you want in a child about to disappear.
   - **Parent (`pid > 0`):** `waitpid(pid, &status, 0)` to block until the child finishes, then inspect status with the standard macros: `WIFEXITED(status)` + `WEXITSTATUS(status)` for normal exit, `WIFSIGNALED(status)` + `WTERMSIG(status)` for signal termination.
4. **Built-ins, handled *before* forking:**
   - `cd <dir>` → `chdir(dir)` directly in the shell process. **Must not be forked** — each process has its own working directory, so a child's `chdir()` has zero effect on the parent once the child exits. This is the single most common "why doesn't cd work" bug.
   - `pwd` → `getcwd()` and print it.
   - `exit` → break the loop / `exit(0)`.

**Milestone:** by end of day 2, `ls`, `echo hello`, `cd ..`, `pwd` all work interactively.

### Days 3–4 — Pipes and I/O redirection (hardest part — budget slack here)

1. **Redirection (`>`, `<`, `>>`):**
   - Parse the line for these tokens, split off the filename.
   - In the **child**, before `exec()`: `open()` the target file with the right flags —
     `O_WRONLY | O_CREAT | O_TRUNC` for `>`, `O_WRONLY | O_CREAT | O_APPEND` for `>>`, `O_RDONLY` for `<`.
   - `dup2(fd, STDOUT_FILENO)` (or `STDIN_FILENO`), then close the original `fd`.
   - Because this happens before `exec()` replaces the process image, the exec'd program inherits the remapped stream transparently.

2. **Pipes (`cmd1 | cmd2`):**
   - `int fds[2]; pipe(fds);` → `fds[0]` = read end, `fds[1]` = write end.
   - Fork **two** children.
   - **First child (producer):** `dup2(fds[1], STDOUT_FILENO)`, then exec cmd1.
   - **Second child (consumer):** `dup2(fds[0], STDIN_FILENO)`, then exec cmd2.
   - **#1 source of bugs:** close the *original* `fds[0]`/`fds[1]` in **both children after dup2'ing**, and close **both** ends in the **parent** entirely. If any process keeps an unused end open, the reader never sees EOF and hangs forever.
   - For multi-stage pipes (`cmd1 | cmd2 | cmd3`), generalize to N−1 pipes for N commands. Plan the fd bookkeeping on paper before coding — this is where it's easy to lose track.

**Milestone:** by end of day 4, `ls > out.txt`, `cat < out.txt`, and `ls | wc -l` all work correctly with no hangs.

### Days 5–6 — Background processes, signals, polish

1. **Background execution (`cmd &`):**
   - Detect the trailing `&`, strip it. Fork/exec as normal, but the parent does **not** `waitpid()` immediately — record the PID in a `Job` list (struct with PID, command string, running/finished) and print the next prompt right away.

2. **Reaping via `SIGCHLD`:**
   - Install with `sigaction()` (preferred over `signal()`).
   - Inside the handler, loop `waitpid(-1, &status, WNOHANG)` until it returns 0 or −1 — a single `SIGCHLD` delivery can represent multiple children finishing near-simultaneously.
   - **Install this handler before forking any background process, ideally at shell startup**, to avoid missing an early `SIGCHLD`.
   - **Keep the handler minimal.** Only a small set of functions are async-signal-safe (`waitpid` is one). `printf`, `std::cout`, `malloc`/`new`, most STL operations are **not** — if a signal interrupts the main program mid-way through one of those, reentering it from the handler can corrupt state and cause rare, hard-to-reproduce crashes. Reap PIDs and set a plain `sig_atomic_t` flag (or update the `Job` list only for the reap itself); do logging/printing back in the main REPL loop.

3. **Ctrl+C (`SIGINT`) handling:**
   - Default `SIGINT` would kill your shell — override it. Pick and implement one clear behavior: forward `SIGINT` to the foreground child (via its PID), or otherwise ensure the shell's own handling doesn't kill the shell while a child runs. Be ready to state exactly which you chose.

4. **Exit-time behavior for background jobs:** if the shell exits while background jobs are still running, they become **orphans** (reparented to PID 1) — not zombies. Decide explicitly:
   - **Option A:** kill remaining jobs on exit (`SIGTERM` to each tracked PID).
   - **Option B:** let them orphan intentionally and keep running (closer to real shell behavior).
   Document whichever you pick in the README.

5. **If behind schedule, cut in this order:** command history → environment variable expansion → job control (`fg`/`bg`). **Never cut** background execution, `SIGCHLD` reaping, or Ctrl+C handling — those are what demonstrate real signal-handling understanding.

---

## 6. Must-Have vs Optional Scope

**MUST HAVE**
REPL · command parsing · `fork()` · `exec()` · `waitpid()` with status macros · `cd`/`pwd`/`exit` · `>` `<` `>>` · pipes (2-stage, ideally multi-stage) · background `&` · `SIGCHLD` (async-signal-safe) · Ctrl+C/`SIGINT` handling · `_exit()` on failed exec · explicit exit-time job behavior.

**OPTIONAL (stretch, cut first)**
Environment variable expansion (`$HOME`, `$PATH`) · command history · job control (`jobs`/`fg`/`bg`) · wildcard/glob expansion · process groups for full job control.

**Deliberately out of scope:** full Bash grammar, shell scripting, command substitution, subshells, a custom scripting language, networking, GUI.

---

## 7. Design Rules to Hold Yourself To

- **Every process closes every fd it doesn't own.** This single rule prevents almost all pipeline hangs and fd leaks.
- **Built-ins never fork.** Decide built-in vs. external *before* any fork logic runs.
- **`_exit()`, not `exit()`, in a child after a failed `exec()`.**
- **Signal handlers do the minimum:** reap + flag, nothing else.
- **`execvp()` already searches `PATH`** — don't reimplement path resolution.

---

## 8. Testing Matrix

| Category | Cases |
|---|---|
| Basic commands | `pwd`, `ls`, `echo hello` |
| Built-ins | `cd ..`, `pwd`, `exit` |
| Redirection | `echo hello > a.txt`, `cat < a.txt`, `echo world >> a.txt` |
| Pipes | `ls \| grep cpp`, `cat a.txt \| sort`, `cat a.txt \| sort \| uniq` |
| Background | `sleep 3 &`, then verify no zombie remains after it finishes |
| Signals | `sleep 20` then Ctrl+C — shell must survive |
| Failure cases | invalid command, missing file for redirection, invalid pipe syntax, multiple simultaneous background jobs, reasoning through `fork()` returning `-1` |
| Exit behavior | start a background job, then `exit` — verify it matches your chosen Section 5.4 behavior |

Where possible, cross-check supported behaviors (exit status, redirection, pipeline output) against a real shell — the goal isn't to clone Bash, just to verify your subset behaves consistently.

---

## 9. Problems You'll Likely Hit

| Problem | Cause | Fix |
|---|---|---|
| `cd` does nothing | Ran in a forked child | Handle built-ins in the parent, before any fork |
| Pipe consumer hangs forever | An fd end wasn't closed somewhere | Close every unused pipe end in every process (both children + parent) |
| Zombies accumulate | No `SIGCHLD` handler, or it doesn't loop `WNOHANG` | Loop `waitpid(-1, &status, WNOHANG)` until it returns 0/-1 |
| Ctrl+C kills the shell | Default `SIGINT` not overridden | Install a `SIGINT` handler; decide forwarding behavior explicitly |
| Redirected file has wrong/garbage content | Wrong `open()` flags (e.g. missing `O_TRUNC`) | `>` truncates, `>>` doesn't — double-check flag combos |
| Duplicated output after failed exec | Used `exit()` instead of `_exit()` | Use `_exit()`/`std::_Exit()` in the child |
| Rare crash inside signal handler | Called non-async-signal-safe functions (`printf`, `malloc`, STL) in the handler | Keep the handler to reap + flag only |
| Multi-stage pipes break at 3+ commands | Off-by-one in fd bookkeeping | For N commands you need N−1 pipes; pipe *i* connects command *i*'s stdout to command *i+1*'s stdin — write it out before coding |
| Background jobs still alive after shell exits | Orphaned, not killed | Implement your chosen Section 5.4 behavior explicitly |

Debugging tools: `gdb`, `strace`, `ps`, `pstree`, `lsof`. `strace ./shellx` is especially useful here since the whole project *is* a system-call learning exercise.

---

## 10. Definition of Done

- [ ] Any external command runs via `fork`/`exec`/`wait`
- [ ] `cd`, `pwd`, `exit` work correctly as built-ins (parent process, no fork)
- [ ] `>`, `>>`, `<` all work correctly with proper file flags
- [ ] Two-stage pipes work with no hangs; multi-stage ideally too
- [ ] Background (`&`) commands don't block the shell
- [ ] Background processes are reaped via `SIGCHLD`, not left as zombies
- [ ] `Ctrl+C` doesn't kill the shell itself
- [ ] `_exit()` used after failed `exec()` in the child
- [ ] Exit-time behavior for lingering background jobs is decided and implemented
- [ ] You can explain every syscall used, and draw the pipe fd flow from memory

---

## 11. Novelty — What Actually Differentiates This

"I built a shell" is a known project category — the differentiator is correctness and depth in the process/signal handling:

1. **Correct fd lifecycle management in pipes** — most rushed implementations have the classic "forgot to close an fd, hangs forever" bug; getting this right (and explaining why) is a genuine signal.
2. **Async-signal-safe `SIGCHLD` reaping**, not just blocking `wait()` everywhere — shows you understand sync vs. async process management, not just the happy path.
3. **Correct built-in vs. forked-command separation** — knowing *why* `cd` can't be external is a small detail that separates real understanding from imitation.

**Strongest story to tell:** *"I implemented Unix process execution and IPC at the system-call layer, correctly handled async-signal-safety constraints in my signal handlers, and diagnosed a file-descriptor lifetime bug that was hanging my pipelines."*

---

## 12. Resume Bullet Draft

**ShellX — Unix Shell & Process Manager | C/C++, Linux, POSIX**
> • Built a Unix shell implementing command execution via `fork()`/`exec()`/`waitpid()`, with file-descriptor-based I/O redirection (`>`, `>>`, `<`) and multi-stage pipe chaining through `dup2()`.
> • Implemented asynchronous, async-signal-safe background process execution with `SIGCHLD`-based zombie reaping and explicit `SIGINT`/Ctrl+C handling.
> • Diagnosed and fixed a file-descriptor leak causing pipeline hangs, and a stdout-duplication bug caused by incorrect child termination (`exit()` vs. `_exit()`) after a failed `exec()`.

---

## 13. Interview Questions You Must Answer Cold

- What does `fork()` do? What does `exec()` do? Why `fork` + `exec`, not just `exec`?
- Why can't `cd` run in a child process?
- What is a file descriptor? What does `dup2()` do?
- How does a pipe work? Why must unused pipe descriptors be closed?
- What is a zombie process? How does `SIGCHLD` help? How is a zombie different from an orphan?
- What happens on Ctrl+C — what did you specifically implement?
- What happens if `fork()` fails? What happens if `exec()` fails?
- Why `_exit()` instead of `exit()` in a child after a failed `exec()`?
- What is async-signal-safety, and why does it matter inside a `SIGCHLD` handler?
- What does your shell do with background jobs still running at shell exit?
- How do you extract the exit code from `waitpid`'s status (`WIFEXITED`/`WEXITSTATUS`)?
- How do you handle a command that doesn't exist?

Be able to draw, from memory, both the fork/exec/wait diagram for a single command and the full fd flow for `ls | grep cpp` — every child, every pipe end, every close.

---

## 14. What You Should Know Before Coding

- **OS:** process, thread, virtual memory basics, context switching, signals, IPC.
- **Linux:** `fork`, `exec`, `waitpid`, `pipe`, `dup2`, `open`, `close`, `kill`.
- **C++:** classes, RAII, vectors/strings, smart pointers, error handling, CMake.

The goal isn't to memorize API signatures — it's to understand what the OS is actually doing when each call executes.
