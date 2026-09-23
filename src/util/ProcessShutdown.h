#pragma once

#include <atomic>

namespace cosmo::util {

// Process-wide cancellation only. Reset before workers are created; never
// reset from a channel reconnect or service destructor. No service ownership
// is attached to this flag, so FFmpeg callbacks remain safe during teardown.
class ProcessShutdown {
public:
    static void ResetForStartup() noexcept {
        requested_.store(false);
    }
    static void Request() noexcept {
        requested_.store(true);
    }
    static bool Requested() noexcept {
        return requested_.load();
    }

private:
    inline static std::atomic<bool> requested_{false};
};

}  // namespace cosmo::util
