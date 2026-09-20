#include "pipeline.hpp"
#include "redirection.hpp"
#include "signals.hpp"
#include <iostream>
#include <vector>
#include <cerrno>

int executePipelineChain(const Pipeline& pipeline) {
    int n = static_cast<int>(pipeline.commands.size());

    // Create N-1 pipes
    // pipes[i] is an array of 2 fds: pipes[i][0] = read end, pipes[i][1] = write end
    std::vector<int> pipe_fds(2 * (n - 1));

    for (int i = 0; i < n - 1; ++i) {
        if (pipe(&pipe_fds[2 * i]) == -1) {
            perror("pipe");
            // Close any pipes we already created
            for (int j = 0; j < 2 * i; ++j) {
                close(pipe_fds[j]);
            }
            return 1;
        }
    }

    // Fork N children
    std::vector<pid_t> child_pids(n);

    for (int i = 0; i < n; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            // Close all pipe fds in parent
            for (int j = 0; j < 2 * (n - 1); ++j) {
                close(pipe_fds[j]);
            }
            // Wait for any children we already forked
            for (int j = 0; j < i; ++j) {
                int status;
                pid_t result;
                do {
                    result = waitpid(child_pids[j], &status, 0);
                } while (result == -1 && errno == EINTR);
            }
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

            // Wire stdin from previous pipe (if not first command)
            if (i > 0) {
                if (dup2(pipe_fds[2 * (i - 1)], STDIN_FILENO) == -1) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // Wire stdout to next pipe (if not last command)
            if (i < n - 1) {
                if (dup2(pipe_fds[2 * i + 1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // Close ALL pipe fds in this child (both ends of every pipe)
            for (int j = 0; j < 2 * (n - 1); ++j) {
                close(pipe_fds[j]);
            }

            // Apply any file redirections (> < >>)
            if (!applyRedirections(pipeline.commands[i])) {
                _exit(1);
            }

            // Build argv for execvp
            const auto& args = pipeline.commands[i].args;
            std::vector<const char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& arg : args) {
                argv.push_back(arg.c_str());
            }
            argv.push_back(nullptr);

            execvp(argv[0], const_cast<char* const*>(argv.data()));

            // exec failed
            perror(argv[0]);
            _exit(errno == ENOENT ? 127 : 126);
        }

        // Parent: record child pid
        child_pids[i] = pid;
    }

    // Parent: close ALL pipe fds immediately after all forks
    for (int j = 0; j < 2 * (n - 1); ++j) {
        close(pipe_fds[j]);
    }

    // Wait for all children. Retry on EINTR.
    // Pipeline exit status = status of the LAST command.
    int pipeline_status = 0;

    for (int i = 0; i < n; ++i) {
        int status;
        pid_t result;
        do {
            result = waitpid(child_pids[i], &status, 0);
        } while (result == -1 && errno == EINTR);

        if (result == -1) {
            perror("waitpid");
            continue;
        }

        // Only the last command's status is reported
        if (i == n - 1) {
            if (WIFEXITED(status)) {
                pipeline_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                pipeline_status = 128 + WTERMSIG(status);
            }
        }
    }

    return pipeline_status;
}
