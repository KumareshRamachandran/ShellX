#include "builtins.hpp"
#include "jobs.hpp"
#include <iostream>
#include <cstdlib>

bool isBuiltin(const std::string& cmd) {
    return (cmd == "cd" || cmd == "pwd" || cmd == "exit" || cmd == "jobs");
}

int executeBuiltin(const Command& cmd) {
    const std::string& name = cmd.args[0];

    if (name == "cd") {
        // cd with no args → go to $HOME
        const char* target = nullptr;
        if (cmd.args.size() < 2) {
            target = std::getenv("HOME");
            if (!target) {
                std::cerr << "cd: HOME not set\n";
                return 1;
            }
        } else {
            target = cmd.args[1].c_str();
        }

        if (chdir(target) != 0) {
            perror("cd");
            return 1;
        }
        return 0;
    }

    if (name == "pwd") {
        char buf[4096];
        if (getcwd(buf, sizeof(buf)) != nullptr) {
            std::cout << buf << "\n";
        } else {
            perror("pwd");
            return 1;
        }
        return 0;
    }

    if (name == "jobs") {
        printJobs();
        return 0;
    }

    if (name == "exit") {
        // Send SIGTERM to all remaining background jobs, no explicit reap
        // (see Final Contract: shell is terminating, orphaned children
        // are cleaned up by init/PID 1).
        killAllJobs();
        return -1; // sentinel: caller should break the REPL loop
    }

    // Should not reach here
    std::cerr << "shellx: unknown builtin: " << name << "\n";
    return 1;
}
