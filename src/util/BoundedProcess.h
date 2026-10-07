#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace cosmo::util {

struct BoundedProcessResult {
    int exitCode{-1};
    std::string output;
    std::string error;
    std::string failure;
};

// No shell, bounded stdin/stdout/stderr, one absolute deadline. Cancellation or
// excess output kills the entire child process group and reaps the direct child.
// Prompts travel on stdin, never in argv. No command/output is logged here.
BoundedProcessResult RunBoundedProcess(const std::vector<std::string>& argv, const std::string& input,
                                       std::chrono::steady_clock::time_point deadline,
                                       const std::function<bool()>& cancelled = {},
                                       size_t outputLimit = 64 * 1024, size_t errorLimit = 8 * 1024);

}  // namespace cosmo::util
