#include "parser.hpp"
#include <iostream>
#include <sstream>

// --- Tokenizer ---
// Splits input on whitespace, handles double-quoted strings, recognizes
// special tokens: |, <, >, >>, &
// Explicitly rejects unsupported constructs: ;, &&, ||, $(), backticks

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    size_t i = 0;
    size_t len = line.size();

    while (i < len) {
        // Skip whitespace
        if (std::isspace(line[i])) {
            ++i;
            continue;
        }

        // Handle double-quoted strings
        if (line[i] == '"') {
            ++i; // skip opening quote
            std::string token;
            while (i < len && line[i] != '"') {
                token += line[i];
                ++i;
            }
            if (i < len) {
                ++i; // skip closing quote
            }
            tokens.push_back(token);
            continue;
        }

        // Handle >> (must check before >)
        if (line[i] == '>' && i + 1 < len && line[i + 1] == '>') {
            tokens.push_back(">>");
            i += 2;
            continue;
        }

        // Handle single-character special tokens: | < > &
        if (line[i] == '|' || line[i] == '<' || line[i] == '>' || line[i] == '&') {
            tokens.push_back(std::string(1, line[i]));
            ++i;
            continue;
        }

        // Regular word
        std::string token;
        while (i < len && !std::isspace(line[i]) &&
               line[i] != '|' && line[i] != '<' && line[i] != '>' &&
               line[i] != '&' && line[i] != '"') {
            token += line[i];
            ++i;
        }
        tokens.push_back(token);
    }

    return tokens;
}

// Check for unsupported constructs and reject them with an error message.
static bool rejectUnsupported(const std::vector<std::string>& tokens) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];

        // ;
        if (t == ";") {
            std::cerr << "shellx: ';' is not supported\n";
            return true;
        }

        // && — appears as two consecutive "&" tokens
        if (t == "&" && i + 1 < tokens.size() && tokens[i + 1] == "&") {
            std::cerr << "shellx: '&&' is not supported\n";
            return true;
        }

        // || — appears as two consecutive "|" tokens
        if (t == "|" && i + 1 < tokens.size() && tokens[i + 1] == "|") {
            std::cerr << "shellx: '||' is not supported\n";
            return true;
        }

        // $(...) — look for $( in any token
        if (t.find("$(") != std::string::npos) {
            std::cerr << "shellx: command substitution '$()' is not supported\n";
            return true;
        }

        // Backticks
        if (t.find('`') != std::string::npos) {
            std::cerr << "shellx: backtick substitution is not supported\n";
            return true;
        }

        // Semicolon embedded in a token
        if (t.find(';') != std::string::npos) {
            std::cerr << "shellx: ';' is not supported\n";
            return true;
        }
    }
    return false;
}

Pipeline parse(const std::string& line) {
    Pipeline pipeline;

    // Tokenize
    std::vector<std::string> tokens = tokenize(line);

    if (tokens.empty()) {
        return pipeline; // empty pipeline = empty input
    }

    // Reject unsupported constructs
    if (rejectUnsupported(tokens)) {
        return pipeline; // empty pipeline signals error
    }

    // Strip trailing '&' → set background flag
    if (!tokens.empty() && tokens.back() == "&") {
        pipeline.background = true;
        tokens.pop_back();
        if (tokens.empty()) {
            std::cerr << "shellx: syntax error near '&'\n";
            return Pipeline{}; // reset to empty
        }
    }

    // Split tokens on '|' into separate commands
    std::vector<std::vector<std::string>> command_token_lists;
    command_token_lists.push_back({});

    for (const auto& token : tokens) {
        if (token == "|") {
            if (command_token_lists.back().empty()) {
                std::cerr << "shellx: syntax error near '|'\n";
                return Pipeline{};
            }
            command_token_lists.push_back({});
        } else {
            command_token_lists.back().push_back(token);
        }
    }

    // Check for trailing pipe
    if (command_token_lists.back().empty()) {
        std::cerr << "shellx: syntax error near '|'\n";
        return Pipeline{};
    }

    // Enforce pipeline length limit
    if (static_cast<int>(command_token_lists.size()) > MAX_PIPELINE_LENGTH) {
        std::cerr << "shellx: pipeline too long (max " << MAX_PIPELINE_LENGTH << " commands)\n";
        return Pipeline{};
    }

    // Parse each command's tokens: extract redirections, build args
    for (auto& cmd_tokens : command_token_lists) {
        Command cmd;

        for (size_t i = 0; i < cmd_tokens.size(); ++i) {
            if (cmd_tokens[i] == "<") {
                // Input redirection
                if (i + 1 >= cmd_tokens.size()) {
                    std::cerr << "shellx: syntax error near '<'\n";
                    return Pipeline{};
                }
                cmd.input_file = cmd_tokens[++i];
            } else if (cmd_tokens[i] == ">" || cmd_tokens[i] == ">>") {
                // Output redirection
                bool append = (cmd_tokens[i] == ">>");
                if (i + 1 >= cmd_tokens.size()) {
                    std::cerr << "shellx: syntax error near '" << cmd_tokens[i] << "'\n";
                    return Pipeline{};
                }
                cmd.output_file = cmd_tokens[++i];
                cmd.append_mode = append;
            } else {
                cmd.args.push_back(cmd_tokens[i]);
            }
        }

        if (cmd.args.empty()) {
            std::cerr << "shellx: empty command in pipeline\n";
            return Pipeline{};
        }

        pipeline.commands.push_back(std::move(cmd));
    }

    return pipeline;
}
