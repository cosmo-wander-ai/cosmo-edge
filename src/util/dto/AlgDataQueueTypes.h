/// @file AlgDataQueueTypes.h
/// @brief POD types for algorithm data queue status reporting.
///        Extracted from flow/common/AlgDataQueue.h to eliminate
///        cross-layer header dependencies (service/dto → flow).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/DurationStat.h"
#include "util/ErrorCode.h"

namespace cosmo {

// ─── Queue Status Info ──────────────────────────────────────────────

/// Per-queue statistical counters — used in AlgDataQueueInfo.
struct AlgDataQueueStatusInfoUnit {
    uint64_t insertCount{0};
    uint64_t processCount{0};
    uint64_t discardCount{0};

    int64_t periodMs{0};
    int durationIndex{0};
    uint64_t insertCountPeriod{0};
    uint64_t processCountPeriod{0};
    uint64_t discardCountPeriod{0};
    uint64_t continuousDiscardCountPeriod{0};

    uint64_t continuousDiscardCount{0};
    uint64_t continuousDiscardCountMax{0};

    uint64_t holdCount{0};
    uint64_t holdCountMax{0};
};

/// External-facing queue status snapshot.
struct AlgDataQueueInfo {
    std::string name;
    size_t queSize{0};
    size_t queLength{0};
    AlgDataQueueStatusInfoUnit status;
};

// ─── Action-level Aggregate ─────────────────────────────────────────

/// Aggregate status for one algorithm action (action → queue → duration).
struct AlgActionDataQueueStatus {
    std::vector<std::string> channelIds;
    std::vector<std::string> taskIds;
    std::string actionId;
    size_t alarmCount{0};
    util::ErrorEnum actionStatus{util::ErrorEnum::Success};
    AlgDataQueueInfo queueStatus;
    std::vector<util::DurationStatInfo> durationInfos;
};

}  // namespace cosmo
