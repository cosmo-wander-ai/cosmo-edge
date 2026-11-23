#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "BenchPhasedGuard.h"
#include "BenchTurnProfile.h"
#include "BenchTurnScheduler.h"
namespace cosmo::bench_ax {
// The old worker enqueues one prepared classifier list, rather than one task
// per ROI. The CPU phase and scheduler wait must run without the AX lock.
enum class PreparedListOutcome { Complete, KnownFailure, CompletionUnknown };
template <class CpuPhase, class Prepare, class Validate, class Run>
PreparedListOutcome RunPreparedHelmetList(std::size_t count, CpuPhase&& cpu_phase, Prepare&& prepare,
                                          Validate&& validate, Run&& run) {
    if (!count || count > 64)
        return PreparedListOutcome::KnownFailure;
    constexpr std::size_t kInputBytes = 150528;
    std::vector<std::vector<std::uint8_t>> inputs;
    std::optional<BenchTurnScheduler::Turn> turn;
    const bool trace = TurnProfileEnabled();
    auto& profile    = TurnProfileState();
    TurnProfileClock::time_point held_start{};
    const auto phase = cpu_phase([&] {
        inputs.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            inputs[i].resize(kInputBytes);
            if (!prepare(i, inputs[i].data(), inputs[i].size()))
                return false;
        }
        const auto wait_start = trace ? TurnProfileClock::now() : TurnProfileClock::time_point{};
        turn.emplace(BenchmarkTurns().Acquire(true));
        if (trace) {
            profile.wait_ns += TurnElapsed(wait_start);
            held_start = TurnProfileClock::now();
        }
        return true;
    });
    if (phase == BenchPhasedGuard::CpuPhaseResult::Poisoned)
        return PreparedListOutcome::CompletionUnknown;
    if (phase != BenchPhasedGuard::CpuPhaseResult::Complete)
        return PreparedListOutcome::KnownFailure;
    for (std::size_t i = 0; i < count; ++i) {
        if (!validate())
            return PreparedListOutcome::KnownFailure;
        const auto status = run(i, inputs[i].data(), inputs[i].size());
        if (status != PreparedListOutcome::Complete)
            return status;
    }
    turn.reset();
    if (trace) {
        ++profile.groups;
        profile.rois += count;
        profile.hold_ns += TurnElapsed(held_start);
    }
    return PreparedListOutcome::Complete;
}
}  // namespace cosmo::bench_ax
