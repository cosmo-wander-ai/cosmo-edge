#pragma once
#include <ax_sys_api.h>

#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "nn/device/axera/benchmark/BenchEngineWorker.h"
#include "nn/device/axera/helmet/HelmetPendingResources.h"

namespace cosmo::bench_ax {
// Per-model noncached CMM, created/grown/destroyed by the central owner.
// The classifier thread exclusively holds FrameLock from host preparation
// through synchronous worker completion and output copying.
class CentralPreparedList final {
public:
    struct Slot {
        helmet_candidate::Block input{}, output{};
    };
    using Ptr = std::shared_ptr<CentralPreparedList>;
    static Ptr Create() {
        return Ptr(new CentralPreparedList, &Destroy);
    }
    std::unique_lock<std::mutex> FrameLock() {
        return std::unique_lock<std::mutex>(frame_mutex_);
    }
    bool Ensure(std::size_t count) {
        if (!count || count > 64)
            return false;
        if (count <= slots_.size())
            return true;
        return BenchmarkEngineWorker().Invoke(true, [this, count] {
            if (owner_ != std::this_thread::get_id())
                return false;
            auto& state = nn::axera::detail::NativeState();
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned)
                return false;
            try {
                while (slots_.size() < count) {
                    // Publish each partially allocated slot before touching the SDK.
                    slots_.emplace_back();
                    auto& slot = slots_.back();
                    for (auto pair : {std::pair<helmet_candidate::Block*, AX_U32>{&slot.input, 150528},
                                      std::pair<helmet_candidate::Block*, AX_U32>{&slot.output, 8}}) {
                        auto& b = *pair.first;
                        b.bytes = pair.second;
                        const auto rc =
                            AX_SYS_MemAlloc(&b.physical, &b.host, b.bytes, 128,
                                            reinterpret_cast<const AX_S8*>("central-helmet-list"));
                        if (rc != 0 || !b.physical || !b.host) {
                            state.poisoned = true;
                            return false;
                        }
                    }
                }
                return true;
            } catch (...) {
                state.poisoned = true;
                return false;
            }
        });
    }
    Slot& At(std::size_t i) {
        return slots_.at(i);
    }

private:
    CentralPreparedList() : owner_(std::this_thread::get_id()) {}
    static void Destroy(CentralPreparedList* p) noexcept {
        if (!p)
            return;
        try {
            auto& state = nn::axera::detail::NativeState();
            std::unique_lock<std::mutex> lock(state.mutex);
            if (state.poisoned || p->owner_ != std::this_thread::get_id()) {
                state.poisoned = true;
                return;
            }
            for (auto& slot : p->slots_)
                for (auto* b : {&slot.output, &slot.input}) {
                    if (!b->physical && !b->host)
                        continue;
                    if (!b->physical || !b->host || AX_SYS_MemFree(b->physical, b->host) != 0) {
                        state.poisoned = true;
                        return;
                    }
                    *b = {};
                }
            lock.unlock();
            delete p;
        } catch (...) { /* Retain allocations after an unprovable release. */
        }
    }
    const std::thread::id owner_;
    std::mutex frame_mutex_;
    std::vector<Slot> slots_;
};
}  // namespace cosmo::bench_ax
