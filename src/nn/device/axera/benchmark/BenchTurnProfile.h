#pragma once
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "util/Log.h"
namespace cosmo::bench_ax {
struct TurnProfile {
    uint64_t groups{}, rois{}, wait_ns{}, hold_ns{}, copy_ns{}, flush_ns{}, run_ns{}, invalidate_ns{};
};
inline bool TurnProfileEnabled() {
    const char* p = std::getenv("COSMO_AXERA_BENCH_LOCK_PROFILE");
    return p && std::strcmp(p, "1") == 0;
}
inline TurnProfile& TurnProfileState() {
    static thread_local TurnProfile p;
    return p;
}
using TurnProfileClock = std::chrono::steady_clock;
inline uint64_t TurnElapsed(TurnProfileClock::time_point t) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(TurnProfileClock::now() - t).count());
}
inline void LogTurnProfile() {
    auto& p = TurnProfileState();
    if (TurnProfileEnabled() && p.groups && p.groups % 32 == 0)
        LOG_INFO(
            "AX_BENCH_CLASS_TURN_PROFILE groups={} rois={} wait_ns={} hold_ns={} copy_ns={} flush_ns={} "
            "run_ns={} invalidate_ns={}",
            p.groups, p.rois, p.wait_ns, p.hold_ns, p.copy_ns, p.flush_ns, p.run_ns, p.invalidate_ns);
}
}  // namespace cosmo::bench_ax
