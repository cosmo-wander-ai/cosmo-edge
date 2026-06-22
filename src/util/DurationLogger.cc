// DurationLogger — Duration logger for performance profiling.

#include "util/DurationLogger.h"

#include <utility>

#include "util/Log.h"

namespace cosmo::util {

DurationLogger::DurationLogger(std::string name) : name_(std::move(name)), start_time_(Clock::now()) {}

DurationLogger::~DurationLogger() {
    if (start_time_ != Clock::time_point{}) {
        int64_t duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time_).count();
        LOG_INFO("duration of {}: {} ms.", name_, duration);
        start_time_ = {};
    }
}

void DurationLogger::Print() {
    if (start_time_ != Clock::time_point{}) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time_).count();
        LOG_INFO("duration of {}: {} ms.", name_, elapsed);
        start_time_ = {};
    }
}

}  // namespace cosmo::util
