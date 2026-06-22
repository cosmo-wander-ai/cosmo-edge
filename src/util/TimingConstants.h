// Centralized timing constants — replace scattered magic-number sleep durations.
#pragma once

#include <chrono>

namespace cosmo::timing {

// --- Poll / spin-wait intervals ---
constexpr auto kSpinWaitInterval   = std::chrono::milliseconds(5);
constexpr auto kFastPollInterval   = std::chrono::milliseconds(10);
constexpr auto kMediumPollInterval = std::chrono::milliseconds(100);
constexpr auto kSlowPollInterval   = std::chrono::milliseconds(200);
constexpr auto kHalfSecondInterval = std::chrono::milliseconds(500);
constexpr auto kOneSecondInterval  = std::chrono::seconds(1);

// --- Startup / shutdown delays ---
constexpr auto kServiceReadyDelay       = std::chrono::seconds(3);
constexpr auto kRebootGracePeriod       = std::chrono::seconds(10);
constexpr auto kThreadSlowStopThreshold = std::chrono::seconds(5);

// --- Encoder-specific ---
constexpr auto kEncoderBusyWait = std::chrono::microseconds(10);

// --- Background Task Intervals ---
constexpr uint64_t kStorageCleanIntervalMs = 5000;

}  // namespace cosmo::timing
