#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "shellx.hpp"

// Execute an N-command pipeline (N >= 2).
// Creates N-1 pipes, forks N children, wires stdin/stdout via dup2(),
// closes all pipe fds in every process, and waits for all children.
// Returns the exit status of the LAST command in the pipeline.
int executePipelineChain(const Pipeline& pipeline);

#endif // PIPELINE_HPP
