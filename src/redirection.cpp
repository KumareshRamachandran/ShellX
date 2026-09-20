#include "redirection.hpp"
#include <cstdio>

bool applyRedirections(const Command& cmd) {
    // --- Input redirection (<) ---
    if (!cmd.input_file.empty()) {
        int fd = open(cmd.input_file.c_str(), O_RDONLY);
        if (fd == -1) {
            perror(cmd.input_file.c_str());
            return false;
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2");
            close(fd);
            return false;
        }
        close(fd); // original fd no longer needed after dup2
    }

    // --- Output redirection (> or >>) ---
    if (!cmd.output_file.empty()) {
        int flags = O_WRONLY | O_CREAT;
        if (cmd.append_mode) {
            flags |= O_APPEND;   // >>
        } else {
            flags |= O_TRUNC;    // >
        }

        int fd = open(cmd.output_file.c_str(), flags, 0644);
        if (fd == -1) {
            perror(cmd.output_file.c_str());
            return false;
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            close(fd);
            return false;
        }
        close(fd); // original fd no longer needed after dup2
    }

    return true;
}
