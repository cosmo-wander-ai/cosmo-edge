#pragma once

#include <cstddef>
#include <mutex>
#include <utility>

#include "ax_sys_api.h"

namespace cosmo::nn {

// Owns a reference to the process-wide AX SYS subsystem. All in-process AX
// users must hold a lease for the entire lifetime of their SDK resources.
// The last lease calls Deinit after those resources have been destroyed.
class AxeraSysLease {
public:
    AxeraSysLease() = default;
    ~AxeraSysLease() {
        Reset();
    }

    AxeraSysLease(const AxeraSysLease&)            = delete;
    AxeraSysLease& operator=(const AxeraSysLease&) = delete;

    AxeraSysLease(AxeraSysLease&& other) noexcept : held_(std::exchange(other.held_, false)) {}
    AxeraSysLease& operator=(AxeraSysLease&& other) noexcept {
        if (this != &other) {
            Reset();
            held_ = std::exchange(other.held_, false);
        }
        return *this;
    }

    // A failed initialization is not cached: a later caller can retry.
    int Acquire() {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (held_)
            return 0;
        if (state.references == 0) {
            const int result = static_cast<int>(AX_SYS_Init());
            if (result != 0)
                return result;
        }
        ++state.references;
        held_ = true;
        return 0;
    }

    void Reset() noexcept {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!held_)
            return;
        held_ = false;
        if (--state.references == 0)
            AX_SYS_Deinit();
    }

    bool Held() const noexcept {
        return held_;
    }

private:
    struct SharedState {
        std::mutex mutex;
        std::size_t references{0};
    };

    static SharedState& State() {
        // Intentionally process-lifetime: avoids static destruction order
        // hazards when a late-destroyed SDK owner releases its lease.
        static auto* state = new SharedState;
        return *state;
    }

    bool held_{false};
};

}  // namespace cosmo::nn
