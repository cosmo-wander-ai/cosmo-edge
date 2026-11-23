#pragma once

#include <thread>

#include "nn/device/axera/benchmark/BenchCachedNv12.h"

namespace cosmo::bench_ax {
// Pure host scratch, never a native lease. It is constructed, accessed and
// used only by the model owner worker; destruction frees ordinary host memory.
class BenchHelmetSnapshotCache final {
public:
    BenchHelmetSnapshotCache() noexcept : owner_(std::this_thread::get_id()) {}
    BenchHelmetSnapshotCache(const BenchHelmetSnapshotCache&)            = delete;
    BenchHelmetSnapshotCache& operator=(const BenchHelmetSnapshotCache&) = delete;
    Nv12Snapshot* ForOwner() noexcept {
        return owner_ == std::this_thread::get_id() ? &snapshot_ : nullptr;
    }

private:
    const std::thread::id owner_;
    Nv12Snapshot snapshot_;
};
}  // namespace cosmo::bench_ax
