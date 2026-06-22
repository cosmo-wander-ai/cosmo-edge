// Task status data types — extracted from flow/task/TaskMng.h and PTaskMng.h
// to break compile-time coupling between service interfaces and flow singletons.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/DurationStat.h"
#include "util/ErrorCode.h"
#include "util/dto/AlgDataQueueTypes.h"

namespace cosmo {

// ---- Queue Status DTO (POD subset extracted from flow/common/AlgDataQueue.h) ----
struct AlgDataQueueInfoDto {
    std::string name;
    size_t queSize{0};
    size_t queLength{0};

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

    AlgDataQueueInfoDto() = default;
    explicit AlgDataQueueInfoDto(const AlgDataQueueInfo& info)
        : name(info.name),
          queSize(info.queSize),
          queLength(info.queLength),
          insertCount(info.status.insertCount),
          processCount(info.status.processCount),
          discardCount(info.status.discardCount),
          periodMs(info.status.periodMs),
          durationIndex(info.status.durationIndex),
          insertCountPeriod(info.status.insertCountPeriod),
          processCountPeriod(info.status.processCountPeriod),
          discardCountPeriod(info.status.discardCountPeriod),
          continuousDiscardCountPeriod(info.status.continuousDiscardCountPeriod),
          continuousDiscardCount(info.status.continuousDiscardCount),
          continuousDiscardCountMax(info.status.continuousDiscardCountMax),
          holdCount(info.status.holdCount),
          holdCountMax(info.status.holdCountMax) {}
};

// ---- Action Queue Status DTO (extracted from flow/common/AlgActionDataQueueStatus.h) ----
struct AlgActionDataQueueStatusDto {
    std::vector<std::string> channelIds;
    std::vector<std::string> taskIds;
    std::string actionId;
    size_t alarmCount{0};
    util::ErrorEnum actionStatus{util::ErrorEnum::Success};
    AlgDataQueueInfoDto queueStatus;
    std::vector<util::DurationStatInfo> durationInfos;

    AlgActionDataQueueStatusDto() = default;
    explicit AlgActionDataQueueStatusDto(const AlgActionDataQueueStatus& info)
        : channelIds(info.channelIds),
          taskIds(info.taskIds),
          actionId(info.actionId),
          alarmCount(info.alarmCount),
          actionStatus(info.actionStatus),
          queueStatus(AlgDataQueueInfoDto(info.queueStatus)),
          durationInfos(info.durationInfos) {}
};

// Extracted from flow/action/AlgActionBase.h (pure POD)
struct ActionRuntimeSon {
    std::string channelId;
    std::string taskId;
    std::string actionId;
    float fps{0.0};
    std::string queueName;
};

struct ActionRuntimeInfo {
    std::string actionId;
    std::string channelId;
    bool bChannelReuse{false};  // NOTE: currently always false; mirrored to ClientMsgError for JSON output
    float maxTaskFps{0.0};
    std::string queueName;
    std::vector<ActionRuntimeSon> sons;
};

struct TaskStatus {
    std::string channelId;
    std::string taskId;
    std::string streamUrl;         // Video stream URL
    std::string algorithmId;       // Algorithm ID
    std::string algorithmName;     // Algorithm name
    std::string algorithmVersion;  // Algorithm version
    std::vector<AlgActionDataQueueStatusDto> queStatus;
    std::vector<ActionRuntimeInfo> actionInfo;
};

struct PTaskStatus {
    std::string taskId;
    std::string streamUrl;         // Video stream URL
    std::string algorithmId;       // Algorithm ID
    std::string algorithmName;     // Algorithm name
    std::string algorithmVersion;  // Algorithm version
    std::vector<AlgActionDataQueueStatusDto> queStatus;
    std::vector<ActionRuntimeInfo> actionInfo;
};

}  // namespace cosmo
