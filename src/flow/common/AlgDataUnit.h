// Asynchronous Queue - Shim Header

#pragma once

#include <chrono>
#include <unordered_map>

#include "flow/common/AlgDetectTypes.h"
#include "util/Log.h"
#include "util/dto/ClientMsgEvent.h"

namespace cosmo {

// Note: AlgDataCopy
struct AlgData {
    AlgDataType dataType{AlgDataType::Invalid};

    std::string channelId;  // Channel data
    std::string taskId;
    bool bHaveTrack{false};
    bool bHaveRelated{false};  // Has human-face association process
    bool bHaveClassify{false};
    bool bHaveLandmark{false};  // Used for recognizer to decide whether to use box (area) or landmark
    bool bHaveLogic{false};     // Used for logic judgement flow in image algorithms
    std::chrono::steady_clock::time_point firstTimePoint;

    // ---- Channel-level data (shared by all tasks) ----
    AlgChannelDataOrig chanDataOrig;      // Raw stream data
    AlgChannelDataDec chanDataDec;        // Decoded data
    AlgChannelDataDetect legacyDetect;    // Last detection result (detector + orchestrator adapter)
    AlgChannelDataDetect chanDataDetect;  // Detection result

    // ---- Unified task data storage (replaces 10+ independent taskDataXxx fields) ----
    // key: AlgDataType (TaskDataTrack, TaskDataClassify, ...), value: Algorithm processing result
    std::unordered_map<AlgDataType, DataDetTrackClassifyPtr> taskResults;

    // Convenience access functions
    DataDetTrackClassifyPtr GetTaskResult(AlgDataType type) const {
        auto it = taskResults.find(type);
        return (it != taskResults.end()) ? it->second : nullptr;
    }
    void SetTaskResult(AlgDataType type, DataDetTrackClassifyPtr data) {
        taskResults[type] = std::move(data);
    }

    // ---- Special structured data (not suitable for taskResults map) ----
    AlgTaskDataAiFilter taskAiFilter;                    // Filter result (with extra fields)
    AlgTaskDataClassifyMultPic taskDataClassifyMultPic;  // Camera moving (with baseFrame)
    AlgTaskDataRecog taskDatarecog;  // Recognition result (completely different structure)
    AlgTaskDataAlarm taskDataAlarm;  // Alarm result (DataAlarmPtr)
};

using AlgDataPtr = std::shared_ptr<AlgData>;

AlgDataPtr AlgDataCopy(AlgDataPtr input);

void TargetSignAreas(DataDetTrackClassifyPtr detRet, const std::vector<MsgTaskArea>& inAreas,
                     const std::vector<MsgTaskArea>& inShieldedAreas, std::vector<AiLabelParam>& labelPos);

std::vector<std::pair<util::Point, util::Point>> GetAreaOsdLines(MsgTaskArea area, int width, int height);

std::vector<std::pair<util::Point, util::Point>> GetAreasOsdLines(const std::vector<MsgTaskArea>& areas,
                                                                  int width, int height);

void ShowAiDetectData(AlgChannelDataDetect& channelDet);

}  // namespace cosmo
