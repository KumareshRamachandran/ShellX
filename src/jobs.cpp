#include "jobs.hpp"
#include "shellx.hpp"
#include <iostream>
#include <algorithm>
#include <sys/wait.h>
#include <signal.h>

// Global job list
std::vector<Job> g_jobs;

int addJob(pid_t pid, const std::string& cmd) {
    if (static_cast<int>(g_jobs.size()) >= MAX_BACKGROUND_JOBS) {
        std::cerr << "shellx: too many background jobs (max " << MAX_BACKGROUND_JOBS << ")\n";
        // Still track it, but warn
    }

    Job job;
    job.pid = pid;
    job.command = cmd;
    job.running = true;

    g_jobs.push_back(job);

    // Job number is 1-based index
    return static_cast<int>(g_jobs.size());
}

void reapFinishedJobs() {
    for (auto it = g_jobs.begin(); it != g_jobs.end(); ) {
        if (!it->running) {
            it = g_jobs.erase(it);
            continue;
        }

        int status;
        pid_t result = waitpid(it->pid, &status, WNOHANG);

        if (result > 0) {
            // Child has exited
            int job_num = static_cast<int>(std::distance(g_jobs.begin(), it)) + 1;
            if (WIFEXITED(status)) {
                std::cout << "[" << job_num << "] Done (exit " << WEXITSTATUS(status)
                          << "): " << it->command << "\n";
            } else if (WIFSIGNALED(status)) {
                std::cout << "[" << job_num << "] Killed (signal " << WTERMSIG(status)
                          << "): " << it->command << "\n";
            }
            it = g_jobs.erase(it);
        } else {
            ++it;
        }
    }
}

void printJobs() {
    int num = 1;
    for (const auto& job : g_jobs) {
        std::cout << "[" << num << "] "
                  << (job.running ? "Running" : "Done")
                  << ": " << job.command
                  << " (pid " << job.pid << ")\n";
        ++num;
    }
    if (g_jobs.empty()) {
        std::cout << "No background jobs\n";
    }
}

void killAllJobs() {
    for (auto& job : g_jobs) {
        if (job.running) {
            kill(job.pid, SIGTERM);
            // Intentionally does NOT wait/reap — shell is exiting,
            // orphaned children are cleaned up by init/PID 1.
        }
    }
}
