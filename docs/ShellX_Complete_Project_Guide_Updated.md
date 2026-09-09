# ShellX
## Unix Shell & Process Manager (Updated)

**Primary purpose:** Build a small Unix shell in C/C++ that directly uses Linux/POSIX process-management and I/O primitives.

This project teaches real OS concepts through implementation rather than theory: `fork()`, `exec()`, `waitpid()`, pipes, file descriptors, redirection, background processes, and signals.

**Time budget: 5-6 days.** Days 1-2 = REPL + core fork/exec/wait loop. Days 3-4 = pipes + redirection (the hardest part — budget slack here). Days 5-6 = background execution, signals, polish. If you're behind schedule, cut in this order: command history → environment variables → job control (`fg`/`bg`) — never cut background execution, `SIGCHLD` reaping, or Ctrl+C handling, since those are what demonstrate real signal-handling understanding.

---

# 1. Why build ShellX?

Your competitive-programming background already demonstrates algorithmic problem solving. ShellX demonstrates something different: algorithms + systems programming + operating systems, together. You'll come out understanding process creation, process replacement, process synchronization, IPC, file descriptors, signals, pipelines, and zombie processes — not from a textbook, but from having implemented and debugged each one.

---

# 2. What ShellX should ultimately do

Start with:
```bash
$ ls
$ pwd
$ echo hello
```
Then support:
```bash
$ cd ..
$ ls > output.txt
$ cat < output.txt
$ ls | grep cpp
$ sleep 10 &
```
Finally: Ctrl+C handling, `SIGCHLD`-based background process reaping.

---

# 3. Technology stack

```text
C++17/20 or C
Linux
POSIX APIs
CMake
```

Key APIs: `fork()`, `execvp()`, `waitpid()`, `pipe()`, `dup2()`, `open()`, `close()`, `chdir()`, `sigaction()`, `_exit()`.

Use Linux natively or WSL. For your profile, C++17/20 is a reasonable choice — stays in a familiar language while learning POSIX.

---

# 4. Repository structure

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
│   └── jobs.hpp
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

`jobs.hpp`/`jobs.cpp` is new — this is where background-job bookkeeping lives (see Section 22).

---

# 5. Architecture

```text
User Input
    |
    v
Lexer / Parser
    |
    v
Command Representation
    |
    +-----------------------+
    |                       |
 Built-in              External command
    |                       |
 execute parent            fork()
                            |
                      +-----+-----+
                      |           |
                   child        parent
                      |           |
                    exec()     waitpid()
```

---

# 6. Stage 1 — REPL

```text
while (true) {
    print prompt
    read input
    parse input
    execute
}
```

**Install your signal handlers (`SIGCHLD`, `SIGINT`) before this loop starts, and definitely before you ever fork a background process** — see Section 23 for why ordering matters here.

---

# 7. Stage 2 — Command parser

```text
ls -la /tmp
```
becomes
```text
argv[0] = "ls"
argv[1] = "-la"
argv[2] = "/tmp"
```

Handle basic quoting eventually (`echo "hello world"`). Do not attempt Bash-compatible parsing — that would dramatically increase project scope beyond what's useful here.

---

# 8. Stage 3 — `fork()`

```cpp
pid_t pid = fork();
```
Return values: `0` → child, child's PID → parent, `-1` → error. The child and parent initially share/inherit relevant process state, including open file descriptors.

**Handle the `-1` error case explicitly** — don't assume `fork()` always succeeds. If it fails (e.g., process table full), report the error and continue the REPL rather than crashing or proceeding as if a child exists.

---

# 9. Stage 4 — `exec()`

```cpp
execvp(argv[0], argv);
```

`execvp()` replaces the current process image entirely.

```text
fork() → creates a process
exec()  → replaces what that process executes
```

**If `execvp()` fails** (e.g., command not found), it returns — meaning the code after it in the child *does* run. Report the error, then terminate the child correctly (see next point — this matters more than it looks).

**Use `_exit()`, not `exit()`, to terminate the child after a failed `exec()`.** This is a genuinely common, easy-to-miss bug: `exit()` flushes C++ stdio buffers and runs `atexit` handlers. If the parent had buffered output not yet flushed at the time of `fork()`, that buffered content can get inherited by the child and **flushed a second time**, producing duplicated output. `_exit()` (or `std::_Exit()`) skips all of that and terminates immediately — which is exactly what you want in a child that's about to disappear anyway. This is a small detail, but it's exactly the kind of thing that shows real process-semantics understanding rather than tutorial-copying.

---

# 10. Stage 5 — `waitpid()`

```cpp
waitpid(pid, &status, 0);
```

Then the shell continues. Inspect the result using the standard macros — know these by name:

- `WIFEXITED(status)` — true if the child exited normally; combine with `WEXITSTATUS(status)` to get the actual exit code.
- `WIFSIGNALED(status)` — true if the child was killed by a signal; combine with `WTERMSIG(status)` to get which signal.

Being able to state these macro names, not just "I checked the exit code," is a good signal of hands-on familiarity rather than surface knowledge.

---

# 11. Stage 6 — Built-in commands

Implement `cd`, `pwd`, `exit`.

**The critical one: `cd`.** Do not execute it in a child — each process has its own working directory, so a child's `chdir()` call has zero effect on the parent shell once that child exits. `cd` must run directly in the shell process, before any fork decision is made for that command.

---

# 12. Stage 7 — File descriptors

```text
0 → stdin
1 → stdout
2 → stderr
```
Then `open()`, `close()`, `dup2()`. A file descriptor is an integer referring to an open resource maintained by the OS.

---

# 13. Stage 8 — Output redirection

```bash
ls > output.txt
```
```text
open("output.txt") → dup2(file_fd, STDOUT_FILENO) → execvp("ls", ...)
```

---

# 14. Input redirection

```bash
sort < input.txt
```
```text
open("input.txt") → dup2(file_fd, STDIN_FILENO) → execvp("sort", ...)
```

---

# 15. Append mode

```bash
echo hello >> output.txt
```
`>` overwrites (use `O_TRUNC`), `>>` appends (use `O_APPEND`) — double-check these flags specifically, a missing `O_TRUNC` is a common source of "why does my file have leftover garbage" bugs.

---

# 16. Stage 9 — Pipes

```bash
ls | grep cpp
```
```cpp
int pipefd[2];
pipe(pipefd);   // pipefd[0] = read end, pipefd[1] = write end
```

---

# 17. Pipe implementation

Two children.

**First child:** `dup2(pipe_write, STDOUT_FILENO)` → `exec(ls)`
**Second child:** `dup2(pipe_read, STDIN_FILENO)` → `exec(grep)`

The parent closes both ends once no longer needed — the parent never reads/writes the pipe directly in a simple two-command pipeline.

---

# 18. Most important pipe bug

If `grep` never terminates: some process still holds an open write end of the pipe, so the reader never sees EOF. **Close unused descriptors in the first child, the second child, and the parent.** This is the single most common shell-implementation bug — document it in your README as a real debugging story if it happens to you (it likely will), including how you diagnosed it.

---

# 19. Multiple pipelines

```bash
cat file.txt | grep foo | sort | uniq
```
For N commands you need N-1 pipes; pipe `i` connects command `i`'s stdout to command `i+1`'s stdin. Write the fd bookkeeping out explicitly on paper before coding — this is where it's genuinely easy to lose track of which fd belongs where.

---

# 20. Stage 10 — Background execution

```bash
sleep 10 &
```
Foreground: `fork` → `wait` → prompt.
Background: `fork` → do **not** immediately wait → prompt (shell can accept another command).

---

# 21. Zombie processes

A child that exits while the parent hasn't yet called `wait()`/`waitpid()` on it retains a process-table entry — a zombie. Not dangerous in small numbers, but a resource leak if it accumulates.

---

# 22. Background job bookkeeping

Don't just informally "track the PID" — define a small explicit structure:

```cpp
struct Job {
    pid_t pid;
    std::string command;
    bool running;
};
std::vector<Job> backgroundJobs;
```

This is what your `SIGCHLD` handler updates when a background job finishes, and it's the natural foundation if you later add the optional `jobs` built-in. Deciding this structure upfront (rather than ad hoc) keeps the signal-handling code in the next section much cleaner.

---

# 23. Stage 11 — `SIGCHLD`

Install a handler that reaps finished children:

```cpp
waitpid(-1, &status, WNOHANG);  // loop until it returns 0 or -1
```

A single `SIGCHLD` delivery can represent multiple children finishing near-simultaneously — always loop the `WNOHANG` wait until nothing more is reapable, don't just reap one and stop.

**Install this handler before you fork any background process, ideally right at shell startup.** If a background child finishes before the handler is installed, its `SIGCHLD` may be missed or handled by default behavior instead of your reaping logic — a real, if narrow, race condition worth being aware of.

**Async-signal-safety — this is a genuinely important constraint, not a nice-to-have.** Signal handlers can only safely call a small set of functions guaranteed to be async-signal-safe (`waitpid` is one of them). Most everyday functions — `printf`, `std::cout`, `malloc`/`new`, most STL container operations — are **not** safe to call from inside a signal handler, because if the signal interrupts the main program mid-way through one of those same operations (e.g., mid-`malloc`), calling it again reentrantly from the handler can corrupt internal state, causing rare, hard-to-reproduce crashes. **Keep your handler minimal: just reap PIDs via `waitpid`/`WNOHANG` and update a plain `sig_atomic_t` flag or a lock-free-safe structure.** Do any logging, printing, or `Job` vector updates back in the main REPL loop by checking that flag — not directly inside the handler.

---

# 24. Stage 12 — Ctrl+C

```text
Ctrl+C → SIGINT
```
Your shell should stay alive while the foreground command terminates. Decide and implement one clear behavior: forward `SIGINT` to the foreground child specifically (e.g., via its PID/process group), or simply ensure the shell's own `SIGINT` handling doesn't kill the shell process itself while a child is running. Be ready to state clearly which behavior you implemented and why — "what happens on Ctrl+C" is a near-guaranteed interview question for this project.

---

# 25. Orphan processes on shell exit

Not covered in most shell tutorials, but worth deciding deliberately: **if you `exit` your shell while background jobs are still running, those children don't die — they get reparented to `init`/PID 1 and become orphans** (a distinct concept from zombies — an orphan is still running, a zombie has exited but not been reaped). Decide your shell's behavior explicitly:
- **Option A:** kill all remaining background jobs on exit (iterate your `Job` vector, send `SIGTERM`).
- **Option B:** let them orphan intentionally and continue running independently (closer to real shell behavior in many cases).

Either is a legitimate, defensible design choice — document which one you picked and why in the README.

---

# 26. Advanced feature — process groups

Optional, only after the basics work. For proper job-control behavior (`jobs`, `fg`, `bg`), processes can be placed into process groups. Cut this first if time is limited.

---

# 27. Features to deliberately skip

Full Bash grammar, shell scripting, command substitution, subshells, advanced job control, custom scripting language, networking, GUI. None of these improve the fundamental learning objective enough to justify the added complexity.

---

# 28. Novelty

A shell isn't novel by itself. Your novelty comes from:

```text
clean process-management implementation
+ correct file-descriptor lifecycle
+ pipeline IPC
+ background process management (with correct async-signal-safe reaping)
+ signal handling
+ good debugging/documentation
```

The strongest story: *"I implemented Unix process execution and IPC from the system-call layer, correctly handled async-signal-safety constraints in my signal handlers, and debugged file-descriptor lifetime issues in pipelines."* That's authentic systems engineering.

---

# 29. Problems you should expect

| Problem | Cause | Solution |
|---|---|---|
| `cd` doesn't work | Executed in a child | Run built-ins in the parent shell, before any fork |
| Pipe hangs forever | Unused write descriptor wasn't closed somewhere | Close every unused pipe end in every process (first child, second child, parent) |
| Zombie accumulation | Background children aren't reaped | `SIGCHLD` handler with looped `WNOHANG` waitpid |
| Ctrl+C kills the shell | Default `SIGINT` behavior not overridden | Install a `SIGINT` handler; decide foreground-forwarding behavior explicitly |
| File descriptor leaks | Descriptors remain open across commands | Track ownership explicitly, close after `dup2()` |
| Duplicated output after a failed exec | Used `exit()` instead of `_exit()` in the child | Use `_exit()`/`std::_Exit()` after a failed `execvp()` |
| Rare crash/corruption in signal handler | Called non-async-signal-safe functions (printf, malloc, STL) inside the handler | Keep handler minimal — reap + flag only, do the rest in the main loop |
| Background jobs still running after shell exits | Orphaned, not killed | Decide and implement explicit exit behavior (Section 25) |

---

# 30. Testing matrix

**Basic commands:** `pwd`, `ls`, `echo hello`
**Built-ins:** `cd ..`, `pwd`, `exit`
**Redirection:** `echo hello > a.txt`, `cat < a.txt`, `echo world >> a.txt`
**Pipes:** `ls | grep cpp`, `cat a.txt | sort`, `cat a.txt | sort | uniq`
**Background:** `sleep 3 &`, then verify no zombie remains after it finishes
**Signals:** `sleep 20` then Ctrl+C — shell must survive
**Failure cases:** invalid command, missing file for redirection/input, invalid pipe syntax, multiple simultaneous background jobs, `fork()` returning -1 (harder to simulate, but reason through the code path)
**Exit behavior:** start a background job, then `exit` the shell — verify it matches your chosen Section 25 behavior

---

# 31. Debugging tools

`gdb`, `strace`, `ps`, `pstree`, `lsof`. `strace ./shellx` shows the actual system calls your process makes — particularly useful since ShellX is fundamentally a system-call learning project; watching your own `fork`/`exec`/`dup2`/`close` calls happen in real time is genuinely clarifying.

---

# 32. What you should learn before coding

**OS:** process, thread, virtual memory basics, context switching, signals, IPC.
**Linux:** `fork`, `exec`, `waitpid`, `pipe`, `dup2`, `open`, `close`, `kill`.
**C++:** classes, RAII, vectors/strings, smart pointers, error handling, CMake.

The objective isn't to memorize APIs — you should understand what the OS is doing when your API call executes.

---

# 33. What ShellX demonstrates

```text
OS: process creation, execution, synchronization, signals
IPC: pipes
File I/O: descriptors, redirection, dup2
Systems programming: POSIX APIs, async-signal-safety
Debugging: process/file-descriptor failures via strace/gdb
```

---

# 34. Resume entry

**ShellX — Unix Shell & Process Manager | C/C++, Linux, POSIX**

> • Built a Unix shell implementing command execution through `fork()`/`exec()`/`waitpid()`, with file-descriptor-based I/O redirection and multi-stage pipelines using `dup2()`.
> • Added background process execution and asynchronous, async-signal-safe child reaping via `SIGCHLD`, alongside parent-side built-ins (`cd`, `pwd`, `exit`) and explicit Ctrl+C/`SIGINT` handling.
> • Diagnosed and resolved a file-descriptor leak causing pipeline hangs, and a stdout-duplication bug from incorrect child-process termination after failed `exec()` calls.

---

# 35. Interview questions you must master

```text
What does fork() do? What does exec() do? Why fork + exec, not just exec?
Why can't cd run in a child?
What is a file descriptor? What does dup2() do?
How does a pipe work? Why must unused pipe descriptors be closed?
What is a zombie process? How does SIGCHLD help?
What's the difference between a zombie and an orphan process?
What happens when Ctrl+C is pressed? What did you implement, specifically?
What happens if fork() fails? What happens if exec() fails?
Why use _exit() instead of exit() after a failed exec() in the child?
What is async-signal-safety, and why does it matter inside a SIGCHLD handler?
What does your shell do with background jobs still running when it exits?
How do you extract the exit code from waitpid's status? (WIFEXITED/WEXITSTATUS)
How do you handle a command that doesn't exist?
```

---

# 36. Final outcome

When ShellX is complete, you should be able to explain this diagram without notes:

```text
                 Shell
                   |
                parse()
                   |
             +-----+------+
             |            |
          builtin      external
             |            |
          execute       fork()
          parent          |
                       child
                         |
                       exec()
                         |
                     program
                         |
                    (success: exit()/normal)
                    (failure: _exit() after error)
                         |
                     waitpid() → WIFEXITED/WEXITSTATUS
```

And for `ls | grep cpp`, you should be able to draw every child, every pipe fd, and every close operation from memory. That is when the project is genuinely yours.

---

# 37. Recommended final scope

**MUST HAVE:** REPL, command parsing, `fork()`, `exec()`, `waitpid()` (with status macros), `cd`, `pwd`, `exit`, `>`, `<`, `>>`, pipes, background `&`, `SIGCHLD` (async-signal-safe), Ctrl+C handling, `_exit()` on failed exec, explicit exit-time job behavior.

**OPTIONAL:** environment variables, command history, process groups, `jobs`/`fg`/`bg`.

Prioritize the MUST HAVE list; cut from OPTIONAL first if time is tight.
