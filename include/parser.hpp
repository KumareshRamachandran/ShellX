#ifndef PARSER_HPP
#define PARSER_HPP

#include "shellx.hpp"
#include <string>

// Parse a raw input line into a Pipeline structure.
// Handles tokenization (whitespace splitting, basic double-quoted strings),
// special token recognition (|, <, >, >>, &), redirection parsing,
// pipe splitting, and grammar boundary enforcement.
// Returns an empty Pipeline (no commands) on parse error or empty input.
Pipeline parse(const std::string& line);

#endif // PARSER_HPP
