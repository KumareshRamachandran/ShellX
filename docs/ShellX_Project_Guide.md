# ShellX — Complete Project Guide
### Unix Shell & Process Manager

**Time budget:** 5-6 days
**Builds on:** nothing prior — genuinely new, but self-contained (no networking, no external dependencies)
**Risk level:** Low-medium — the concepts are new, but the tools (POSIX syscalls) are well-documented and testable entirely on your own machine

---

## 1. What This Project Actually Is

A working command-line shell — like a stripped-down `bash` — that can run programs, chain them together with pipes, redirect input/output to files, and manage foreground/background processes. This is the classic "build your own shell" project used in real OS coursework, because it forces you to actually use process management syscalls instead of just reading about them.

---

## 2. Tools & Tech Stack

| Component | Choice | Why |
|---|---|---|
| Language | C or C++ | POSIX syscalls are C APIs either way; C++ is fine if you prefer RAII-style cleanup, plain C is more "traditional" for this kind of project — either is defensible |
| Environment | Linux (native or WSL) | POSIX syscalls (`fork`, `exec`, `pipe`, signals) are Linux/Unix-specific — this won't work natively on Windows |
| Core headers | `unistd.h`, `sys/wait.h`, `signal.h`, `fcntl.h`, `sys/types.h` | Standard POSIX process/IO/signal APIs |
| Build system | CMake or a simple Makefile | Either is fine; Makefile is arguably more "authentic" for a systems project like this |
| Testing | Manual interactive testing + a few scripted test cases | No fancy test framework needed — a shell is naturally interactive-testable |

---

## 3. Architecture

```
                         User types a command
                                 │
                          Read input line
                                 │
                    Parse into tokens / argv[]
                    (handle quotes, |, <, >, &)
                                 │
                 ┌───────────────┼────────────────┐
                 ▼               ▼                ▼
           Built-in command   Single command   Piped command
           (cd, pwd, exit)    (fork+exec)       (fork x N + pipes)
                 │               │                │
           run in parent    child execs,      chain fds between
           (no fork)        parent waits      children via pipe()
                                 │                │
                          Handle redirection  Close unused fds
                          via dup2() before   in parent & children
                          exec() in child
                                 │
                       Foreground: wait()
                       Background (&): track PID,
                       reap via SIGCHLD handler
                                 │
                          Print next prompt
```

---

## 4. Features — Full Breakdown

### Core (must-have — don't cut these under time pressure)
- **Command parsing** — tokenize a line into `argv[]`, handling whitespace and basic quoting.
- **Process execution** — `fork()` + `execvp()` + `waitpid()` for running any external program.
- **Built-in commands** — `cd`, `pwd`, `exit`, run directly in the parent shell process (not forked).
- **I/O redirection** — `>` (overwrite output), `>>` (append output), `<` (input from file), via `open()` + `dup2()`.
- **Pipes** — `cmd1 | cmd2` (and ideally multi-stage `cmd1 | cmd2 | cmd3`), via `pipe()` + `dup2()` + correct fd closing.
- **Background execution** — `cmd &` doesn't block the shell; track the PID.
- **Zombie reaping** — a `SIGCHLD` handler that reaps finished background processes asynchronously.
- **Ctrl+C handling** — `SIGINT` should kill the running foreground child, not your shell process itself.

### Stretch (cut first if behind schedule)
- Environment variable expansion (`$HOME`, `$PATH` substitution).
- Command history (up-arrow recall, or even just a simple `history` builtin storing past commands).
- Job control (`jobs`, `fg`, `bg` to manage multiple background processes).
- Wildcard/glob expansion (`*.txt`).

---

## 5. Build Plan (Day by Day)

### Days 1-2 — Core execution loop
1. **Read-eval loop skeleton:** print a prompt, read a line (`fgets` or `getline`), loop.
2. **Tokenizer:** split the line into an `argv[]`-style array of strings on whitespace (handle basic quoted strings if you want polish, but don't over-invest here).
3. **`fork()` + `execvp()` + `waitpid()`:**
   - `pid_t pid = fork();`
   - In the child (`pid == 0`): call `execvp(argv[0], argv)` — this replaces the child's process image with the target program. If `execvp` returns at all, it failed (print an error and `exit(1)` in the child).
   - In the parent (`pid > 0`): call `waitpid(pid, &status, 0)` to block until the child finishes, and check the exit status.
4. **Built-ins, handled *before* forking:**
   - `cd <dir>` → call `chdir(dir)` directly in the shell process. **This must not be forked** — a child process changing its own working directory has zero effect on the parent shell, since each process has its own independent working directory. This is the single most common "why doesn't cd work" bug in shell implementations.
   - `pwd` → `getcwd()` and print it.
   - `exit` → break the main loop / call `exit(0)`.

**Milestone check:** by end of day 2, you should be able to run `ls`, `echo hello`, `cd ..`, `pwd` interactively.

### Days 3-4 — Pipes and I/O redirection
This is the hardest part conceptually — budget real time and expect to debug.

1. **Redirection (`>`, `<`, `>>`):**
   - Parse the command line for these tokens and split off the filename.
   - In the **child**, before calling `exec()`: use `open()` to get a file descriptor for the target file (with appropriate flags — `O_WRONLY | O_CREAT | O_TRUNC` for `>`, `O_WRONLY | O_CREAT | O_APPEND` for `>>`, `O_RDONLY` for `<`).
   - Use `dup2(fd, STDOUT_FILENO)` (or `STDIN_FILENO` for input) to remap the process's standard stream to that file, then close the original `fd`.
   - Because this happens *before* `exec()` replaces the process image, the exec'd program inherits the remapped file descriptor transparently — it just writes to "stdout" as normal, not knowing it's actually a file.

2. **Pipes (`cmd1 | cmd2`):**
   - `int fds[2]; pipe(fds);` — creates a connected read/write file descriptor pair: `fds[0]` for reading, `fds[1]` for writing.
   - Fork **two** children (one per command).
   - **First child** (producer): `dup2(fds[1], STDOUT_FILENO)` — its stdout now writes into the pipe. Then `exec()` the first command.
   - **Second child** (consumer): `dup2(fds[0], STDIN_FILENO)` — its stdin now reads from the pipe. Then `exec()` the second command.
   - **Critical step, the #1 source of bugs:** close the *original* `fds[0]` and `fds[1]` in **both children after dup2'ing**, and close **both** in the **parent** entirely (the parent never reads/writes the pipe directly). If any process keeps an unused end of the pipe open, the reading end never sees EOF, and the consumer process hangs forever waiting for more input that will never come.
   - For multi-stage pipes (`cmd1 | cmd2 | cmd3`), generalize this into a loop creating N-1 pipes for N commands — plan the fd bookkeeping on paper before coding it, this is where it gets genuinely easy to lose track.

**Milestone check:** by end of day 4, `ls > out.txt`, `cat < out.txt`, and `ls | wc -l` should all work correctly, with no hangs.

### Days 5-6 — Background processes, signals, and polish
1. **Background execution (`cmd &`):**
   - Detect the trailing `&`, strip it from the parsed command.
   - Fork and exec as normal, but the parent does **not** call `waitpid()` immediately — instead, record the child's PID (e.g., in a small vector of "background jobs") and immediately print the next prompt.

2. **Reaping background processes (`SIGCHLD`):**
   - Install a signal handler for `SIGCHLD` using `sigaction()` (preferred over the older `signal()` for reliable behavior).
   - Inside the handler, loop calling `waitpid(-1, &status, WNOHANG)` to reap *any* finished child without blocking — a background process finishing sends `SIGCHLD` asynchronously, and your handler's job is to clean it up so it doesn't become a zombie.
   - Keep the handler itself minimal (signal handlers should do very little work) — just reap and maybe log/update your jobs list.

3. **Ctrl+C (`SIGINT`) handling:**
   - By default, `SIGINT` would kill your shell process itself (bad — you don't want Ctrl+C to close the whole shell).
   - Install a handler in the shell that, instead, forwards the interrupt to the *currently running foreground child* (if any), or simply ignores it if no foreground command is running — leaving the shell prompt intact.

4. **If you're behind schedule, cut in this order:** command history first, then environment variable expansion, then job control (`fg`/`bg`) — the four core items above (background execution, `SIGCHLD` reaping, Ctrl+C handling) are worth keeping even under time pressure, since they're what demonstrates real signal-handling understanding.

---

## 6. Novelty — What Makes This Stand Out

"I built a shell" is a known category of project, so the differentiator here isn't the concept — it's the **completeness and correctness of the process/signal handling**, which is exactly what separates a real implementation from a copy-pasted tutorial:

1. **Correct fd lifecycle management in the pipe implementation** — most rushed shell implementations have the classic "forgot to close an fd, process hangs forever" bug. Getting this right (and being able to explain *why* it matters) is a genuine differentiator.
2. **Asynchronous zombie reaping via `SIGCHLD`**, not just blocking `wait()` on everything — this shows you understand the difference between synchronous and asynchronous process management, not just the happy path.
3. **Correct built-in vs. forked-command separation** — understanding *why* `cd` can't be an external program is a small but telling detail that separates genuine understanding from surface-level imitation.

---

## 7. Problems You'll Likely Hit (and how to think about them)

| Problem | Why it happens | How to think through it |
|---|---|---|
| Pipe consumer hangs forever | An fd end of the pipe wasn't closed somewhere (often in the parent, or the "wrong" child) | Draw out every fd and which process holds it at each step, on paper, before debugging in code — this bug is almost always a missing `close()` call |
| `cd` appears to do nothing | You forked before calling `chdir()` | Move built-in handling to before the fork check entirely — built-ins never fork |
| Background processes become zombies | No `SIGCHLD` handler, or handler doesn't loop with `WNOHANG` | A single `SIGCHLD` can represent multiple simultaneously-finished children — always loop `waitpid(-1, &status, WNOHANG)` until it returns 0 or -1, don't just reap one |
| Ctrl+C kills your whole shell | Default `SIGINT` behavior wasn't overridden | Install a `SIGINT` handler in the shell itself; decide explicitly whether it should forward to a foreground child or just reprint the prompt |
| Redirection works but the file has garbage/wrong content | Wrong `open()` flags (e.g., missing `O_TRUNC` so old file contents linger) | Double check flag combinations for each redirection type — `>` should truncate, `>>` should not |
| Multi-stage pipes work for 2 commands but break for 3+ | Off-by-one in the fd bookkeeping loop | Generalize carefully: for N commands you need N-1 pipes, and pipe `i` connects command `i`'s stdout to command `i+1`'s stdin — write this out explicitly before coding |

---

## 8. Learning Outcomes (map these explicitly when talking about the project)

- **Process creation and lifecycle** — `fork()`, `exec()` family, `wait()`/`waitpid()`.
- **File descriptors** — how they're inherited across `fork()`, how `dup2()` remaps them, why closing unused ends matters.
- **Inter-process communication basics** — pipes as a kernel-buffered fd pair connecting two processes.
- **Signals** — asynchronous notification (`SIGCHLD`), signal handler design, `SIGINT` handling.
- **The distinction between shell built-ins and external programs** — and why that distinction exists at all.

---

## 9. Resume Bullet Draft

> **ShellX — Unix Shell & Process Manager | C/C++, Linux, POSIX**
> • Built a Unix shell implementing command execution via `fork()`/`exec()`/`waitpid()`, with I/O redirection (`>`, `>>`, `<`) and multi-stage pipe chaining implemented through `dup2()`-based file descriptor remapping.
> • Implemented asynchronous background process execution with `SIGCHLD`-based zombie reaping and `SIGINT` handling to isolate foreground-child interrupts from the shell process itself.
> • Diagnosed and fixed file-descriptor leaks in the pipe implementation causing consumer processes to hang indefinitely, reinforcing correct fd lifecycle management across forked processes.

---

## 10. Interview Defense — Questions You Must Answer Cold

- **"What does `fork()` actually do?"**
  → Duplicates the calling process — same code, same memory contents (copy-on-write in practice), same open file descriptors. Returns `0` in the child, the child's PID in the parent, `-1` on failure.

- **"Why do you need both `fork()` and `exec()` — why not just `exec()`?"**
  → `exec()` replaces the *current* process's image entirely — if the shell called `exec()` directly, the shell itself would be replaced and never return to prompt for the next command. `fork()` creates a new process first, so `exec()` in the child leaves the parent (the shell) untouched and still running.

- **"Why must `cd` be a built-in, not an external program?"**
  → Each process has its own working directory. If `cd` ran as a forked child process, calling `chdir()` there would only change *that child's* working directory — which then exits immediately, having no effect on the parent shell at all.

- **"Walk me through exactly what happens for `cmd1 | cmd2`."**
  → Be ready to literally draw it: `pipe()` creates fds[0]/fds[1]; fork child A, `dup2(fds[1], stdout)` in A then exec cmd1; fork child B, `dup2(fds[0], stdin)` in B then exec cmd2; close both original fd ends in both children after dup2'ing, and close both in the parent entirely.

- **"What's a zombie process, and how did you avoid it?"**
  → A zombie is a process that has exited but whose exit status hasn't been collected via `wait()`/`waitpid()` yet — it still occupies a process table entry. For background jobs, I avoided this with a `SIGCHLD` handler that reaps asynchronously via `waitpid(-1, &status, WNOHANG)` in a loop.

- **"What happens if you `Ctrl+C` while a foreground command is running?"**
  → Explain your actual chosen behavior clearly — whether you forward `SIGINT` to the foreground child specifically, or handle it at the shell level — and why default behavior (killing the shell itself) would be wrong for a shell to exhibit.

- **"What's the difference between blocking and non-blocking `wait()`?"**
  → `waitpid(pid, &status, 0)` blocks until that specific child exits; `waitpid(-1, &status, WNOHANG)` returns immediately if no child has exited yet, letting you poll/handle multiple children without stalling — this is what makes async `SIGCHLD` reaping possible.

---

## 11. Definition of Done
- [ ] Can execute any external command via `fork`/`exec`/`wait`
- [ ] `cd`, `pwd`, `exit` work correctly as built-ins (parent process, no fork)
- [ ] `>`, `>>`, `<` redirection all work correctly with proper file flags
- [ ] Two-stage pipes work with no hangs; ideally multi-stage pipes work too
- [ ] Background (`&`) commands don't block the shell
- [ ] Background processes are reaped via `SIGCHLD`, not left as zombies
- [ ] `Ctrl+C` doesn't kill the shell itself
- [ ] You can explain every syscall used, and draw the pipe fd flow from memory
