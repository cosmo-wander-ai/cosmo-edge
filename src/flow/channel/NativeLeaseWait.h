#pragma once

#include <chrono>

namespace cosmo {
// Owner-thread admission only. No device submission occurs while readiness is
// false; the caller treats cancellation/timeout as a discontinuity.
template <typename Running, typename Ready, typename Now, typename Sleep>
bool WaitForNativeInput(Running running, Ready ready, Now now, Sleep sleep,
                        std::chrono::steady_clock::duration limit) {
    const auto deadline = now() + limit;
    while (running()) {
        if (ready())
            return true;
        if (now() >= deadline)
            return false;
        sleep();
    }
    return false;
}
}  // namespace cosmo
