// Asynchronous Queue - Shim Header

#pragma once

#include <chrono>
#include <map>
#include <unordered_map>

#include "flow/common/AlarmPrivacySnapshot.h"
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
#include "flow/classify/NativeHelmetTicket.h"
#endif
#include "flow/common/AlgDetectTypes.h"
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
#include "flow/classify/NativeFaceFeatureProof.h"
#endif
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
#include "flow/classify/NativeBodyFeatureProof.h"
#endif
#include "util/Log.h"
#include "util/dto/ClientMsgEvent.h"
#include "util/dto/VlmResult.h"
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
#include "nn/device/axera/axera_face_diagnostics.h"
#endif

namespace cosmo {

struct VisualDecisionAudit {
    std::shared_ptr<service::VisualDecisionRun> run;
    std::vector<nlohmann::json> records;
    std::vector<std::shared_ptr<service::VisualAuditLease>> leases;
};

// Note: AlgDataCopy
struct AlgData {
    AlgDataType dataType{AlgDataType::Invalid};
#ifdef COSMO_AXERA_REID_ROUTE_CANDIDATE
    std::unique_ptr<NativeBodyFeatureProof> native_body_feature_proof;
#endif
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
    std::unique_ptr<NativeFaceFeatureProof> native_face_feature_proof;
#endif
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    // Explicit move-only task handoff; AlgDataCopy deliberately never copies it.
    std::unique_ptr<NativeHelmetTicket> native_helmet_ticket;
#endif
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    // Diagnostic-only provenance, never used as inference authority.
    std::shared_ptr<const nn::axera::diagnostics::FrameKey> native_diagnostic_key;
#endif

    std::string channelId;  // Channel data
    std::string taskId;
    std::string visualFrameId;
    std::vector<VisualDecisionAudit> visualDecisions;
    std::vector<VlmResult> vlmResults;
    bool bHaveTrack{false};
    bool bHaveRelated{false};  // Has human-face association process
    bool bHaveClassify{false};
    bool bHaveLandmark{false};  // Used for recognizer to decide whether to use box (area) or landmark
    bool bHaveLogic{false};     // Used for logic judgement flow in image algorithms
    // Request-local picture rule state. Copied at forks; never shared between requests.
    std::map<std::string, std::map<std::string, std::string>> pictureRules;
    std::map<std::string, std::string> pictureDecisions;
    std::string pictureDecision;
    bool pictureBranch{true};
    std::chrono::steady_clock::time_point firstTimePoint;

    // ---- Channel-level data (shared by all tasks) ----
    AlgChannelDataOrig chanDataOrig;      // Raw stream data
    AlgChannelDataDec chanDataDec;        // Decoded data
    AlgChannelDataDetect legacyDetect;    // Last detection result (detector + orchestrator adapter)
    AlgChannelDataDetect chanDataDetect;  // Detection result

    std::shared_ptr<const AlarmPrivacySnapshot> alarmPrivacySnapshot;
    bool alarmPrivacyUnavailable{false};

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
