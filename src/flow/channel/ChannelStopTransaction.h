#pragma once

#include <mutex>
#include <shared_mutex>

namespace cosmo {
// The stop callback may join workers whose notifications read channel data.
// Never hold data_mutex over that callback; competing lifecycle calls fail.
template <typename MayStop, typename StopWorkers>
bool RunChannelStopTransaction(std::mutex& lifecycle_mutex, std::shared_mutex& data_mutex, bool& started,
                               bool& transitioning, MayStop may_stop, StopWorkers stop_workers,
                               bool needs_cleanup = false) {
    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex, std::try_to_lock);
    if (!lifecycle_lock.owns_lock())
        return false;
    {
        std::lock_guard<std::shared_mutex> lock(data_mutex);
        if ((!started && !needs_cleanup) || transitioning || !may_stop())
            return false;
        transitioning = true;
    }
    bool clean = false;
    try {
        clean = stop_workers();
    } catch (...) {
        clean = false;
    }
    {
        std::lock_guard<std::shared_mutex> lock(data_mutex);
        if (clean)
            started = false;
        transitioning = false;
    }
    return clean;
}
}  // namespace cosmo
