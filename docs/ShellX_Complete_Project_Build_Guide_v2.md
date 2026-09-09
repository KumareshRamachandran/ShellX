# ShellX
## Unix Shell & Process Manager

**Primary purpose:** Build a small Unix shell in C/C++ that directly uses Linux/POSIX process-management and I/O primitives.

This project teaches real OS concepts through implementation rather than theory: `fork()`, `exec()`, `waitpid()`, pipes, file descriptors, redirection, background processes and signals. The supplied plan deliberately keeps the project self-contained and Linux/POSIX based.

---

# 1. Why build ShellX?

Your competitive-programming background already demonstrates algorithmic problem solving.

ShellX demonstrates something different:

```text
Algorithms
   +
Systems programming
   +
Operating systems
```

The project lets you understand:

```text
Process creation
Process replacement
Process synchronization
IPC
File descriptors
Signals
Pipelines
Zombie processes
```

That is excellent general-SDE preparation.

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

Finally:

```text
Ctrl+C
SIGCHLD
background process handling
```

---

# 3. Technology stack

Use:

```text
C++ or C
Linux
POSIX APIs
CMake
```

Important APIs:

```text
fork()
execvp()
waitpid()
pipe()
dup2()
open()
close()
chdir()
signal()/sigaction()
```

The supplied guide recommends Linux or WSL.

For your profile, I would personally use **C++17/20** so you remain in a familiar language while learning POSIX.

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
│   └── signals.hpp
├── src/
│   ├── main.cpp
│   ├── parser.cpp
│   ├── executor.cpp
│   ├── builtins.cpp
│   ├── redirection.cpp
│   ├── pipeline.cpp
│   └── signals.cpp
└── tests/
```

Keep modules small.

---

# 5. Architecture

Final architecture:

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

For pipelines:

```text
cmd1
 |
pipe
 |
cmd2
```

For redirection:

```text
stdout
   ↓
file
```

---

# 6. Stage 1 — REPL

The shell runs continuously:

```text
while (true) {
    print prompt
    read input
    parse input
    execute
}
```

Example:

```text
ShellX> ls
...
ShellX> pwd
...
ShellX>
```

---

# 7. Stage 2 — Command parser

Start simple.

Input:

```text
ls -la /tmp
```

becomes:

```text
argv[0] = "ls"
argv[1] = "-la"
argv[2] = "/tmp"
```

Handle basic quoting eventually:

```bash
echo "hello world"
```

Do not attempt Bash-compatible parsing.

That would dramatically increase the project scope.

---

# 8. Stage 3 — `fork()`

For external commands:

```cpp
pid_t pid = fork();
```

Result:

```text
Parent
 |
 +---- Child
```

Return values:

```text
0           → child
child PID   → parent
-1          → error
```

The child and parent initially inherit relevant process state, including open file descriptors.

---

# 9. Stage 4 — `exec()`

The child calls:

```cpp
execvp(argv[0], argv);
```

`execvp()` replaces the current process image.

Important distinction:

```text
fork()
→ creates a process

exec()
→ replaces what that process executes
```

The combination gives you:

```text
Shell
  |
 fork
  |
 Child
  |
exec(ls)
  |
ls
```

The supplied plan explicitly identifies this distinction as a critical interview question. 
---

# 10. Stage 5 — `waitpid()`

The parent waits for foreground commands:

```cpp
waitpid(pid, &status, 0);
```

Then the shell continues.

You should also inspect:

```text
normal exit
signal termination
exit code
```

This teaches parent-child synchronization.

---

# 11. Stage 6 — Built-in commands

Implement:

```text
cd
pwd
exit
```

### The critical one: `cd`

Do **not** execute `cd` in a child.

Why?

```text
Shell parent
 |
 fork
 |
Child
 |
chdir(...)
```

The child's working directory changes.

The shell parent's directory does not.

Therefore:

```text
cd
```

must execute directly in the shell process.

This is one of the most valuable ShellX interview questions.

---

# 12. Stage 7 — File descriptors

Learn these first:

```text
0 → stdin
1 → stdout
2 → stderr
```

Then understand:

```text
open()
close()
dup2()
```

A file descriptor is simply an integer referring to an open resource maintained by the OS.

---

# 13. Stage 8 — Output redirection

Support:

```bash
ls > output.txt
```

Flow:

```text
open("output.txt")
        ↓
dup2(file_fd, STDOUT_FILENO)
        ↓
execvp("ls", ...)
```

Now `ls` writes into the file instead of the terminal.

---

# 14. Input redirection

Support:

```bash
sort < input.txt
```

Flow:

```text
open("input.txt")
       ↓
dup2(file_fd, STDIN_FILENO)
       ↓
execvp("sort", ...)
```

---

# 15. Append mode

Support:

```bash
echo hello >> output.txt
```

Use the appropriate open flags.

The concept is:

```text
>
overwrite

>>
append
```

---

# 16. Stage 9 — Pipes

Support:

```bash
ls | grep cpp
```

Create a pipe:

```cpp
int pipefd[2];
pipe(pipefd);
```

Where:

```text
pipefd[0] = read end
pipefd[1] = write end
```

Architecture:

```text
        PIPE
      +-------+
      |       |
ls → write   read → grep
      |       |
      +-------+
```

---

# 17. Pipe implementation

You need two children.

### First child

```text
stdout
 ↓
dup2(pipe_write, STDOUT_FILENO)
 ↓
exec(ls)
```

### Second child

```text
stdin
 ↓
dup2(pipe_read, STDIN_FILENO)
 ↓
exec(grep)
```

The parent closes both ends once they are no longer needed.

---

# 18. Most important pipe bug

Suppose `grep` never terminates.

Why?

One process may still have an open write end.

Then the reader never sees EOF.

You must carefully close unused descriptors in:

```text
first child
second child
parent
```

The supplied plan explicitly identifies this as the most common shell-implementation bug.

This is an excellent real debugging experience to document in your README.

---

# 19. Multiple pipelines

Eventually support:

```bash
cat file.txt | grep foo | sort | uniq
```

Architecture:

```text
cat
 ↓
pipe
 ↓
grep
 ↓
pipe
 ↓
sort
 ↓
pipe
 ↓
uniq
```

You don't need sophisticated shell grammar.

Represent the command as:

```text
Pipeline
 ├── Command
 ├── Command
 ├── Command
 └── Command
```

Then create the required pipes and children.

---

# 20. Stage 10 — Background execution

Support:

```bash
sleep 10 &
```

Normal foreground:

```text
fork
 ↓
wait
 ↓
prompt
```

Background:

```text
fork
 ↓
do NOT immediately wait
 ↓
prompt
```

Now the shell can accept another command.

---

# 21. Zombie processes

A child may exit while the parent continues.

If the parent doesn't reap it:

```text
child exits
   ↓
zombie
```

The process isn't executing anymore but retains a process-table entry until reaped.

---

# 22. Stage 11 — `SIGCHLD`

Install handling for:

```text
SIGCHLD
```

When children terminate, use `waitpid()` appropriately to reap them.

You should be careful around signal-handler restrictions and shared state; keep the first implementation simple.

The supplied project plan explicitly includes asynchronous zombie-process reaping using `SIGCHLD`.

---

# 23. Stage 12 — Ctrl+C

Press:

```text
Ctrl+C
```

This normally generates:

```text
SIGINT
```

Your shell should remain alive while the foreground command can be terminated.

Conceptually:

```text
Shell
   |
foreground child
   |
Ctrl+C
   ↓
SIGINT
   ↓
child terminates
   ↓
Shell continues
```

This teaches signal handling and process relationships.

---

# 24. Advanced feature — process groups

Only after the basics work.

For proper job-control behavior, processes can be placed into process groups.

This can support:

```text
jobs
fg
bg
```

But this is **optional**.

The project plan explicitly recommends cutting job control first if time is limited.

---

# 25. Features to deliberately skip

Don't add these initially:

```text
full Bash grammar
shell scripting
command substitution
subshells
advanced job control
custom scripting language
networking
GUI
```

They don't improve the fundamental learning objective enough to justify the complexity.

---

# 26. Novelty

A shell isn't novel by itself.

Your novelty comes from:

```text
clean process-management implementation
+
correct file-descriptor lifecycle
+
pipeline IPC
+
background process management
+
signal handling
+
good debugging/documentation
```

The strongest story is:

> "I implemented Unix process execution and IPC from the system-call layer and debugged file-descriptor lifetime issues in pipelines."

That's authentic systems engineering.

---

# 27. Problems you should expect

## Problem 1 — `cd` doesn't work

Cause:

You executed it in a child.

Solution:

Run built-ins in the parent shell.

---

## Problem 2 — Pipe hangs

Cause:

Unused write descriptor wasn't closed.

Solution:

Close every unused pipe end in every process.

---

## Problem 3 — Zombie accumulation

Cause:

Background children aren't reaped.

Solution:

Use `waitpid()` / `SIGCHLD`.

---

## Problem 4 — Ctrl+C kills the shell

Cause:

Shell and foreground command aren't being handled correctly.

Solution:

Understand signal delivery and foreground process behavior.

---

## Problem 5 — File descriptor leaks

Cause:

Descriptors remain open across commands.

Solution:

Track ownership explicitly and close descriptors after `dup2()` when appropriate.

---

# 28. Testing matrix

### Basic commands

```bash
pwd
ls
echo hello
```

### Built-ins

```bash
cd ..
pwd
exit
```

### Redirection

```bash
echo hello > a.txt
cat < a.txt
echo world >> a.txt
```

### Pipes

```bash
ls | grep cpp
cat a.txt | sort
cat a.txt | sort | uniq
```

### Background

```bash
sleep 3 &
```

### Signals

```bash
sleep 20
Ctrl+C
```

### Failure cases

```text
invalid command
missing file
invalid pipe
broken redirection
multiple background jobs
```

---

# 29. Debugging tools

Learn to use:

```text
gdb
strace
ps
pstree
lsof
```

These can dramatically improve your understanding.

For example:

```bash
strace ./shellx
```

can show system calls made by the process.

That's particularly useful because ShellX is fundamentally a system-call learning project.

---

# 30. What you should learn before coding

### OS

```text
process
thread
virtual memory basics
context switching
signals
IPC
```

### Linux

```text
fork
exec
waitpid
pipe
dup2
open
close
kill
```

### C++

```text
classes
RAII
vectors/strings
smart pointers
error handling
CMake
```

The objective isn't to memorize APIs.

You should understand **what the OS is doing when your API call executes**.

---

# 31. What ShellX demonstrates

```text
OS
 ├── process creation
 ├── process execution
 ├── synchronization
 └── signals

IPC
 └── pipes

File I/O
 ├── descriptors
 ├── redirection
 └── dup2

Systems programming
 └── POSIX APIs

Debugging
 └── process/file-descriptor failures
```

---

# 32. Resume entry

**ShellX — Unix Shell & Process Manager | C/C++, Linux, POSIX**

> • Built a Unix shell implementing command execution through `fork()`/`exec()`/`waitpid()`, with file-descriptor-based I/O redirection and multi-stage pipelines using `dup2()`.  
> • Added background process execution and asynchronous child reaping with `SIGCHLD`, while implementing parent-side built-ins such as `cd`, `pwd`, and `exit`.

This is aligned with the supplied project plan.

---

# 33. Interview questions you must master

```text
What does fork() do?

What does exec() do?

Why fork + exec?

Why can't cd run in a child?

What is a file descriptor?

What does dup2() do?

How does a pipe work?

Why must unused pipe descriptors be closed?

What is a zombie process?

How does SIGCHLD help?

What happens when Ctrl+C is pressed?

How are foreground and background processes different?

What happens if fork() fails?

What happens if exec() fails?

How do you handle a command that doesn't exist?
```

The supplied plan highlights the fork/exec distinction, `cd`, pipes, file descriptors, and zombie processes as the key technical questions.

---

# 34. Final outcome

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
                       exit()
                         |
                     waitpid()
```

And for:

```bash
ls | grep cpp
```

you should be able to draw:

```text
Shell
 ├── child 1 → ls
 │       stdout
 │          ↓
 │        PIPE
 │          ↓
 └── child 2 → grep
         stdin
```

with every file descriptor and close operation explained.

That is when the project is genuinely yours.

---

# 35. Recommended final scope

### MUST HAVE

```text
[✓] REPL
[✓] command parsing
[✓] fork()
[✓] exec()
[✓] waitpid()
[✓] cd
[✓] pwd
[✓] exit
[✓] >
[✓] <
[✓] >>
[✓] pipes
[✓] background &
[✓] SIGCHLD
[✓] Ctrl+C
```

### OPTIONAL

```text
[ ] environment variables
[ ] command history
[ ] process groups
[ ] jobs
[ ] fg
[ ] bg
```

The supplied plan specifically recommends prioritizing the first group and cutting the advanced features if time is tight.

---

# 36. Missing but Important — Process Groups and Terminal Control

There is one important distinction in a Unix shell:

```text
process
vs
process group
vs
foreground job
```

Your initial implementation can work without full job control, but you should understand the concept.

For a pipeline:

```text
ls | grep cpp | sort
```

the commands logically form one foreground job.

A mature shell associates the pipeline with a process group so terminal-generated signals such as `Ctrl+C` can be delivered to the whole foreground job.

Do not implement full job control unless you have time, but understand why it exists.

---

# 37. Missing but Important — `SIGCHLD` Correctness

Be careful about doing too much inside a signal handler.

Signal handlers have restrictions on what operations are safe.

A robust pattern is to let the handler do minimal notification/reaping work using async-signal-safe operations, while keeping complex state management in the normal execution path where possible.

For a learning project, document this explicitly rather than building a large amount of logic inside `SIGCHLD`.

This is a good interview topic:

> “Why is signal-handler code intentionally small?”

---

# 38. Missing but Important — `exec()` Failure

`execvp()` only returns when it fails.

Therefore the child should handle failure explicitly:

```text
fork()
  ↓
child
  ↓
execvp()
  ↓
failure?
  ↓
print error
_exit(...)
```

The child should not accidentally continue executing the shell's normal control flow after a failed `exec()`.

This is a very common correctness issue.

---

# 39. Missing but Important — `fork()` Failure

`fork()` can fail because the OS cannot create another process.

Your shell should:

```text
detect failure
report error
continue running
```

Do not assume process creation always succeeds.

---

# 40. Missing but Important — File Descriptor Discipline

Create a simple rule:

> Every process closes every descriptor it does not own.

For example, after:

```text
dup2(pipe_write, STDOUT_FILENO)
```

the duplicated descriptor and original descriptor refer to the same open file description; close the original when it is no longer required.

Track descriptor ownership explicitly.

This is the core technique that prevents pipeline hangs and leaks.

---

# 41. Missing but Important — `FD_CLOEXEC`

Understand the idea of:

```text
close-on-exec
```

If your shell accidentally leaves unrelated file descriptors open across `exec()`, the executed program may inherit resources it shouldn't.

You do not need an elaborate descriptor framework, but understanding `FD_CLOEXEC` is valuable for the interview.

---

# 42. Missing but Important — `PATH`

`execvp()` searches `PATH` for commands.

Therefore these behave differently conceptually:

```text
./program
```

versus:

```text
program
```

Understand:

```text
PATH
absolute path
relative path
```

You do not need to implement your own PATH search because `execvp()` already provides it.

---

# 43. Missing but Important — Exit Status

After `waitpid()` inspect the status.

Distinguish:

```text
normal exit
signal termination
exit code
```

Conceptually:

```text
exited normally?
  → exit status

terminated by signal?
  → signal number
```

This makes the shell more correct and gives you another OS interview topic.

---

# 44. Missing but Important — `errno`

System calls can fail.

For:

```text
fork
exec
open
pipe
dup2
waitpid
chdir
```

learn the basic pattern:

```text
return value
+
errno
+
perror()/strerror()
```

Do not ignore system-call failures.

---

# 45. Missing but Important — EINTR

Some system calls may be interrupted by signals.

You should at least understand the idea of:

```text
EINTR
```

and know that some blocking calls may need to be retried depending on the API and context.

You do not need to build a giant retry framework.

The important thing is knowing that:

> signals can interrupt blocking system calls.

---

# 46. Missing but Important — Parser Boundaries

Do not promise “shell parsing” if your parser only handles whitespace.

Clearly define the supported grammar:

```text
commands
arguments
basic quotes
|
>
<
>>
&
```

Then reject unsupported constructs cleanly.

This makes the project more credible than pretending to implement Bash.

---

# 47. Missing but Important — Pipeline Resource Limits

A malformed command can request a very large pipeline.

You can impose simple bounds such as:

```text
maximum pipeline commands
maximum command-line length
maximum background jobs
```

This teaches resource-conscious design without adding much code.

---

# 48. Missing but Important — Testing Against the Real Shell

For supported behavior, compare ShellX with a standard shell on representative cases.

For example:

```text
command exit status
redirection behavior
pipeline output
```

The goal is not to clone Bash.

The goal is to verify that your supported subset behaves consistently.

---

# 49. Missing but Important — Debugging Workflow

For each difficult bug:

```text
reproduce
→ isolate
→ inspect system calls
→ form hypothesis
→ patch
→ regression test
```

Useful tools:

```text
gdb
strace
lsof
ps
pstree
```

Keep a small “debugging notes” section in the README with one or two genuine bugs you encountered.

---

# 50. ShellX — Stronger Final Interview Story

The completed project should demonstrate:

```text
Processes
System calls
IPC
File descriptors
Signals
Synchronization
Linux debugging
Failure handling
Resource ownership
```

Your strongest project explanation becomes:

> “I implemented a Unix-shell subset directly on Linux system calls. The most useful debugging problem was a pipeline that hung because an unused write descriptor remained open; tracing the descriptor lifecycle showed why the reader never received EOF.”
