// Duration logger for performance profiling.
#pragma once

#include <chrono>
#include <string>

namespace cosmo::util {
class DurationLogger {
public:
    using Clock = std::chrono::high_resolution_clock;

    explicit DurationLogger(std::string name);

    ~DurationLogger();

    void Print();

private:
    std::string name_;
    Clock::time_point start_time_;
};
}  // namespace cosmo::util
