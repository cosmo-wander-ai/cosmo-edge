#pragma once

/*
 * @Author: zhangxiaobo
 * @Date: 2023-11-29 15:56:48
 * @LastEditors: zhangxiaobo
 * @LastEditTime: 2025-08-18 10:06:55
 * @Description:
 */

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/ClientMsgEvent.h"
namespace cosmo {

enum class FaceLogicCaptureStrategy {
    kBest = 0,  // Optimal snapshot
    kRealtime,  // Real-time snapshot
    kMax        //
};
constexpr bool ValidateCaptureStrategy(int value) noexcept {
    return value >= static_cast<int>(FaceLogicCaptureStrategy::kBest) &&
           value < static_cast<int>(FaceLogicCaptureStrategy::kMax);
}

constexpr bool ValidateRealtimeDuration(int value) noexcept {
    return value >= 0 && value <= 10000;
}

constexpr bool ValidateQuality(float value) noexcept {
    return value >= 0.0f && value <= 100.0f;
}
struct BAFaceLogicParam {
    FaceLogicCaptureStrategy capture_strategy{FaceLogicCaptureStrategy::kBest};  // Snapshot strategy
    int realtime_duration{3};                                          // Real-time snapshot duration
    float capture_quality{60};                                         // Snapshot quality
    bool in_out_picture{false};                                        // Need in/out pictures
    std::vector<FaceAngle> capture_angles{FaceAngle::FaceAngleFront};  // Snapshot angles
};

class FaceLogic : public AlgActionBase {
public:
    FaceLogic(const std::string &taskId, ActionNode &actionFaceLogic);
    ~FaceLogic();

    void QueueStatus(std::vector<AlgActionDataQueueStatus> &queStatus,
                     unsigned int durationSec = 30) override;

    // Modify parameters based on existing ones
    bool ModifyParam(const std::string &channelId, const std::string &taskId,
                     std::vector<MsgDynamicKeyValue> &params) override;
    // Set parameters - clear previous ones and set fully new ones
    bool SetParam(const std::string &channelId, const std::string &taskId,
                  std::vector<MsgDynamicKeyValue> &params) override;

    void HandFrame(AlgDataPtr algData) override;

protected:
    // Subclasses override run in threads
    void run() override;

private:
    class TrackIdData;
    bool AnalysisKey(MsgDynamicKeyValue &param);

    [[nodiscard]] bool FindAngleInConfig(FaceAngle &angle);
    void AddHistory(AlgDataPtr algData, VideoFramePtr frame, DataDetTrackClassifyPtr input,
                    const std::chrono::steady_clock::time_point &dataTimePoint);
    void OldTrackId(AlgDataPtr dataPtr);

    [[nodiscard]] bool GetQualityAngle(AiDetectRstEl &target, const std::vector<AiConfidence> &classifyRst,
                                       float &quality, FaceAngle &angle, const util::Box &box);
    bool GetTargteQualityAngle(bool bHaveRelated, AiDetectRstEl &target, float &quality, FaceAngle &angle);

    // Optimal snapshot
    void HandBestFace(AlgDataPtr algData, TrackIdData &trackIdData, const std::string &areaId,
                      const std::string &areaName);
    // Real-time snapshot
    void HandRealtimeFace(AlgDataPtr algData, TrackIdData &trackIdData,
                          const std::chrono::steady_clock::time_point &dataTimePoint);

    void PushData(AlgDataPtr dataPtr, TrackIdData &trackIdData, const std::string &tag,
                  OnEventsReportType reportType, const std::string &areaId, const std::string &areaName);

    // Record entering area
    void RecordIntoArea(TrackIdData &trackIdData, AiDetectRstEl &target, VideoFramePtr frame);
    // Handle exiting area
    void HandOutArea(AlgDataPtr dataPtr, TrackIdData &trackIdData, const AiDetectRstEl &target,
                     VideoFramePtr frame);
    void HandOutAreaWithDisapear(AlgDataPtr dataPtr, TrackIdData &trackIdData, VideoFramePtr frame);

    size_t filter_frames_{0};
    BAFaceLogicParam params_;
    DataDetTrackClassify det_data_status_;
    std::map<unsigned, TrackIdData> track_id_status_map_;
};
using FaceLogicPtr = std::shared_ptr<FaceLogic>;
}  // namespace cosmo
