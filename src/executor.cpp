#include "executor.hpp"
#include "builtins.hpp"
#include "redirection.hpp"
#include "pipeline.hpp"
#include "jobs.hpp"
#include "signals.hpp"
#include <iostream>
#include <vector>
#include <cerrno>
#include <sstream>

// Execute a single (non-pipeline) external command via fork/exec/wait.
static int executeSingle(const Command& cmd, bool background) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // --- Child process ---

        // Restore SIGINT to default so the child responds normally to Ctrl+C
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);

        // Apply I/O redirections (>, >>, <)
        if (!applyRedirections(cmd)) {
            _exit(1); // _exit(), not exit() — avoid double-flushing stdio buffers
        }

        // Build argv for execvp
        std::vector<const char*> argv;
        argv.reserve(cmd.args.size() + 1);
        for (const auto& arg : cmd.args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        // execvp searches PATH — don't reimplement path resolution
        execvp(argv[0], const_cast<char* const*>(argv.data()));

        // exec failed — perror + _exit with appropriate code
        perror(argv[0]);
        if (errno == ENOENT) {
            _exit(127); // command not found
        } else {
            _exit(126); // found but not executable (EACCES or other)
        }
    }

    // --- Parent process ---

    if (background) {
        // Background: add to job list, print job info, return immediately
        std::ostringstream oss;
        for (size_t j = 0; j < cmd.args.size(); ++j) {
            if (j > 0) oss << " ";
            oss << cmd.args[j];
        }
        int job_num = addJob(pid, oss.str());
        std::cout << "[" << job_num << "] " << pid << "\n";
        return 0;
    }

    // Foreground: blocking waitpid with explicit EINTR retry.
    // A background child finishing (delivering SIGCHLD) while we're blocked
    // here for a foreground child is a real, expected occurrence.
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        // Child was killed by a signal (e.g. SIGINT from Ctrl+C)
        return 128 + WTERMSIG(status);
    }

    return 1;
}

int executePipeline(const Pipeline& pipeline) {
    if (pipeline.commands.empty()) {
        return 0;
    }

    // Single command: check for built-in first (before any fork)
    if (pipeline.commands.size() == 1) {
        const Command& cmd = pipeline.commands[0];

        if (isBuiltin(cmd.args[0])) {
            // Built-ins run in parent — never fork
            return executeBuiltin(cmd);
        }

        // Single external command
        return executeSingle(cmd, pipeline.background);
    }

    // Multi-command pipeline
    if (pipeline.background) {
        // Backgrounded pipelines are out of scope for this pass.
        // Job tracks one PID; a backgrounded pipeline needs multi-PID job tracking.
        std::cerr << "shellx: backgrounded pipelines (cmd1 | cmd2 &) are not supported\n";
        return 1;
    }

    return executePipelineChain(pipeline);
}
