#pragma once

#include <atomic>
#include <mutex>

namespace cosmo::nn::axera::detail {
// Shared by experimental native executors. A failure with unknown completion
// prevents a different native executor from reusing the process device state.
// Other AX users are not yet integrated with this quarantine boundary.
struct NativeExecutionState {
    std::mutex mutex;
    // IVPS and Engine can process different buffers concurrently. A
    // poison check must not hold up IVPS behind synchronous engine calls.
    std::atomic<bool> poisoned{false};
};
inline NativeExecutionState& NativeState() {
    static auto* state = new NativeExecutionState;
    return *state;
}
}  // namespace cosmo::nn::axera::detail
