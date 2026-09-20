#ifndef SIGNALS_HPP
#define SIGNALS_HPP

#include <signal.h>

// --- Global signal flags (async-signal-safe types) ---
// Written ONLY by signal handlers; read by normal code in the REPL loop.
extern volatile sig_atomic_t g_sigchld_pending;
extern volatile sig_atomic_t g_sigint_received;

// Install SIGCHLD and SIGINT handlers via sigaction().
// Must be called once at shell startup, before the REPL loop and before any fork().
void installSignalHandlers();

#endif // SIGNALS_HPP
