#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <thread>

namespace cosmo::bench_ax {
// Only host bookkeeping lives here. The CMM blocks and SDK/model owners are
// intentionally preserved if their safe destruction cannot be established.
// Prepare is called with NativeState::mutex already held by BenchPhasedGuard.
template <class Sdk, class Block, class State>
class BenchHelmetIoCache final {
public:
    using Ptr = std::shared_ptr<BenchHelmetIoCache>;
    static Ptr Create(std::shared_ptr<Sdk> sdk, std::shared_ptr<const void> model,
                      std::shared_ptr<const void> bytes, State& state) {
        if (!sdk || !model || !bytes)
            return {};
        return Ptr(new BenchHelmetIoCache(std::move(sdk), std::move(model), std::move(bytes), state),
                   &BenchHelmetIoCache::Delete);
    }
    BenchHelmetIoCache(const BenchHelmetIoCache&)            = delete;
    BenchHelmetIoCache& operator=(const BenchHelmetIoCache&) = delete;

    bool Prepare(std::array<Block, 3>& target) {
        if (owner_ != std::this_thread::get_id())
            return false;
        constexpr unsigned sizes[]{150528, 8};
        for (int i = 0; i < 2; ++i) {
            auto& b = blocks_[i];
            if (b.physical || b.host) {
                if (!b.physical || !b.host || b.bytes != sizes[i])
                    return false;
            } else {
                b.bytes       = sizes[i];
                const auto rc = sdk_->Allocate(&b.physical, &b.host, b.bytes);
                target[i]     = b;  // retain every partially returned allocation
                if (rc != 0 || !b.physical || !b.host)
                    return false;
            }
            target[i] = b;
        }
        return true;
    }

private:
    BenchHelmetIoCache(std::shared_ptr<Sdk> sdk, std::shared_ptr<const void> model,
                       std::shared_ptr<const void> bytes, State& state)
        : sdk_(std::move(sdk)),
          model_(std::move(model)),
          bytes_(std::move(bytes)),
          state_(&state),
          owner_(std::this_thread::get_id()) {}
    static void Delete(BenchHelmetIoCache* p) noexcept {
        if (!p)
            return;
        try {
            std::unique_lock<std::mutex> lock(p->state_->mutex);
            if (p->owner_ != std::this_thread::get_id()) {
                p->state_->poisoned = true;
                return;
            }
            if (p->state_->poisoned)
                return;
            for (int i = 1; i >= 0; --i) {
                auto& b = p->blocks_[i];
                if (!b.physical && !b.host)
                    continue;
                if (!b.physical || !b.host || p->sdk_->Free(b.physical, b.host) != 0) {
                    p->state_->poisoned = true;
                    return;
                }
                b = {};
            }
            lock.unlock();
            // The model owner's destructor may also take NativeState::mutex.
            delete p;
        } catch (...) {
            // An exception during release has unknown completion. Keep the
            // entire cache and its underlying owners indefinitely.
            try {
                std::lock_guard<std::mutex> lock(p->state_->mutex);
                p->state_->poisoned = true;
            } catch (...) {
            }
        }
    }
    const std::shared_ptr<Sdk> sdk_;
    const std::shared_ptr<const void> model_, bytes_;
    State* const state_;
    const std::thread::id owner_;
    std::array<Block, 2> blocks_{};
};
}  // namespace cosmo::bench_ax
