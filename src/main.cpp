#include "shellx.hpp"
#include "parser.hpp"
#include "executor.hpp"
#include "signals.hpp"
#include "jobs.hpp"
#include <iostream>
#include <string>

int main() {
    // Install signal handlers BEFORE the REPL loop and before any fork().
    // A background child finishing before handler installation would miss
    // SIGCHLD, leaving a zombie.
    installSignalHandlers();

    std::string line;
    int last_status = 0;

    while (true) {
        // Check for SIGCHLD: reap finished background jobs
        if (g_sigchld_pending) {
            g_sigchld_pending = 0;
            reapFinishedJobs();
        }

        // Check for SIGINT: if no foreground child, re-print prompt
        if (g_sigint_received) {
            g_sigint_received = 0;
            std::cout << "\n"; // move to a new line after ^C
        }

        // Print prompt
        std::cout << "shellx> " << std::flush;

        // Read input
        if (!std::getline(std::cin, line)) {
            // EOF (Ctrl+D) — exit gracefully
            std::cout << "\n";
            killAllJobs();
            break;
        }

        // Enforce MAX_INPUT_LENGTH explicitly — getline() itself has no length cap
        if (line.size() > MAX_INPUT_LENGTH) {
            std::cerr << "shellx: input too long (max " << MAX_INPUT_LENGTH << " characters)\n";
            continue;
        }

        // Skip empty input
        if (line.empty()) {
            continue;
        }

        // Skip whitespace-only input
        bool all_whitespace = true;
        for (char c : line) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                all_whitespace = false;
                break;
            }
        }
        if (all_whitespace) {
            continue;
        }

        // Parse
        Pipeline pipeline = parse(line);
        if (pipeline.commands.empty()) {
            // Parse error or empty result — error message already printed
            continue;
        }

        // Execute
        last_status = executePipeline(pipeline);

        // Check for exit sentinel
        if (last_status == -1) {
            break;
        }

        // Reap any background jobs that finished during execution
        if (g_sigchld_pending) {
            g_sigchld_pending = 0;
            reapFinishedJobs();
        }
    }

    return last_status >= 0 ? last_status : 0;
}
