#ifndef JOBS_HPP
#define JOBS_HPP

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
extern std::vector<Job> g_jobs;

// Add a background job to the job list. Returns the job number (1-based).
int addJob(pid_t pid, const std::string& cmd);

// Called from the REPL after g_sigchld_pending is set.
// Loops through the job list, calls waitpid(pid, &status, WNOHANG) per running job,
// prints completion messages, and removes finished jobs.
void reapFinishedJobs();

// Print all jobs and their status.
void printJobs();

// Send SIGTERM to all running background jobs on shell exit.
// Does NOT wait for them to terminate or reap them — the shell process is
// exiting immediately afterward, so orphaned/terminated children are cleaned
// up by init/PID 1.
void killAllJobs();

#endif // JOBS_HPP
