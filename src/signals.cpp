#include "signals.hpp"
#include <cstring>

// --- Global signal flags ---
volatile sig_atomic_t g_sigchld_pending = 0;
volatile sig_atomic_t g_sigint_received = 0;

// SIGCHLD handler — notification only, no reaping.
// Sets a flag and returns immediately. Does NOT call waitpid(), printf,
// malloc, new, or any STL operation — async-signal-safety is mandatory.
static void sigchld_handler(int /*signo*/) {
    g_sigchld_pending = 1;
}

// SIGINT handler — does NOT kill the shell.
// Sets a flag so the REPL can re-print the prompt if no foreground child is running.
static void sigint_handler(int /*signo*/) {
    g_sigint_received = 1;
}

void installSignalHandlers() {
    struct sigaction sa;

    // --- SIGCHLD: notification-only handler ---
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    // SA_RESTART:    auto-restart interrupted syscalls (belt; EINTR retry is suspenders)
    // SA_NOCLDSTOP:  don't signal when children are stopped, only when they exit
    sigaction(SIGCHLD, &sa, nullptr);

    // --- SIGINT: shell survives Ctrl+C ---
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
}
