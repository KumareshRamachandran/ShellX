#ifndef EXECUTOR_HPP
#define EXECUTOR_HPP

#include "shellx.hpp"

// Main dispatch: execute a parsed pipeline.
// - Single command + builtin → executeBuiltin()
// - Single command → executeSingle()
// - Multi-command → executePipelineChain()
// Returns the exit status of the pipeline (last command's status),
// or -1 as a sentinel to break the REPL loop (from "exit" builtin).
int executePipeline(const Pipeline& pipeline);

#endif // EXECUTOR_HPP
