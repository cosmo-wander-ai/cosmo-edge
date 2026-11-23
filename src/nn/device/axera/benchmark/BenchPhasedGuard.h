#pragma once
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "BenchTurnProfile.h"
#include "nn/device/axera/helmet/HelmetPendingResources.h"
#include "util/Log.h"

namespace cosmo::bench_ax {
// Benchmark-only phased guard. The original shared guard remains untouched.
// A separate retained owner is created before any SDK operation. On unknown
// completion it intentionally retains the same owners, matching the original
// guard's quarantine policy.
class BenchPhasedGuard final {
public:
    enum class CpuPhaseResult { Complete, KnownFailure, Poisoned };
    template <class Operation, class Cleanup>
    static helmet_candidate::GuardResult Run(std::unique_ptr<helmet_candidate::PendingResources> input,
                                             Operation&& op, Cleanup&& cleanup) {
        using namespace helmet_candidate;
        if (!input || !input->frame || !input->frame->Valid() || !input->frame->frame_witness ||
            !input->frame->frame_witness->Matches(*input->frame) || !input->model || !input->io_owner ||
            !input->mapping_owner || !input->sdk || !input->frame_lease || !input->task_permit ||
            !input->model_permit || !input->handle || input->owner_thread != std::this_thread::get_id() ||
            input->model->HandleForOwnerThread() != input->handle)
            return GuardResult::InvalidResources;
        auto p      = std::shared_ptr<PendingResources>(std::move(input));
        auto anchor = std::make_unique<std::shared_ptr<PendingResources>>(p);
        struct Profile {
            uint64_t count{}, initial_ns{}, reacquire_ns{}, reacquire_calls{};
        };
        static thread_local Profile prof;
        using Clock           = std::chrono::steady_clock;
        const char* trace_opt = std::getenv("COSMO_AXERA_BENCH_LOCK_PROFILE");
        const bool trace      = trace_opt && std::strcmp(trace_opt, "1") == 0;
        auto ns               = [](Clock::time_point t) {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count());
        };
        auto& global             = nn::axera::detail::NativeState();
        const auto initial_start = trace ? Clock::now() : Clock::time_point{};
        std::unique_lock<std::mutex> lock(global.mutex);
        if (trace)
            prof.initial_ns += ns(initial_start);
        if (global.poisoned)
            return GuardResult::Poisoned;
        GuardResult result = GuardResult::CompletionUnknown;
        {
            struct Guard {
                nn::axera::detail::NativeExecutionState& state;
                std::unique_ptr<std::shared_ptr<PendingResources>>& anchor;
                bool known{false};
                ~Guard() {
                    if (known)
                        anchor.reset();
                    else {
                        state.poisoned = true;
                        (void)anchor.release();
                    }
                }
            } guard{global, anchor};
            auto cpu_phase = [&](auto&& convert) {
                lock.unlock();
                bool ok = false;
                try {
                    ok = convert();
                } catch (...) {
                    lock.lock();
                    throw;
                }
                const auto reacquire_start = trace ? Clock::now() : Clock::time_point{};
                lock.lock();
                if (trace) {
                    prof.reacquire_ns += ns(reacquire_start);
                    ++prof.reacquire_calls;
                }
                if (global.poisoned)
                    return CpuPhaseResult::Poisoned;
                return ok ? CpuPhaseResult::Complete : CpuPhaseResult::KnownFailure;
            };
            try {
                auto status = std::forward<Operation>(op)(*p, cpu_phase);
                if (status != DeviceOperation::CompletionUnknown && std::forward<Cleanup>(cleanup)(*p)) {
                    guard.known = true;
                    result      = status == DeviceOperation::Complete ? GuardResult::Complete
                                                                      : GuardResult::KnownFailure;
                }
            } catch (...) { /* Ambiguous ownership: retain p and poison. */
            }
        }
        lock.unlock();
        LogTurnProfile();
        if (trace && ++prof.count % 32 == 0)
            LOG_INFO(
                "AX_BENCH_CLASS_LOCK_PROFILE count={} initial_wait_ns={} reacquire_calls={} "
                "reacquire_wait_ns={}",
                prof.count, prof.initial_ns, prof.reacquire_calls, prof.reacquire_ns);
        return result;
    }
};
}  // namespace cosmo::bench_ax
