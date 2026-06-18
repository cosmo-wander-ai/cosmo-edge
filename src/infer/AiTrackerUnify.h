#pragma once

#include "infer/AiCommon.h"
#include "media/VideoFrame.h"
#include "nn/utils/tracker_wrap.h"
#include "util/ErrorCode.h"

namespace cosmo {

struct TrackLabel {
    int class_id{0};
    std::string label;
};

class AiTrackerUnify {
public:
    AiTrackerUnify(const std::string& atomic_code, std::vector<std::string> labels,
                   std::vector<AiConfidence> confidence, int max_width = 1920, int max_height = 1080);
    ~AiTrackerUnify();

    util::ErrorEnum Trace(std::vector<AiDetectRstEl>& input, std::vector<AiDetectRstEl>& rst);

    util::ErrorEnum SetConfidence(std::vector<AiConfidence> confidence);
    util::ErrorEnum SetConfidence(AiConfidence confidences);

    util::ErrorEnum SetConfig(float motion_iou = -1.0, int motion_length = -1, double dynamic_match = -0.1);

private:
    std::vector<cosmo::nn::TrackingBox> DetEl2TrackEl(std::vector<AiDetectRstEl>& input);
    std::vector<AiDetectRstEl> TrackEl2DetEl(std::vector<cosmo::nn::TrackingBox>& input);
    void TrackDataSignDetId(std::vector<AiDetectRstEl>& track_out, const std::vector<AiDetectRstEl>& input);

    AITrackingStatus AITrackStatusChange(cosmo::nn::TrackingStatus status);
    AIMotionState AIMotionStatusChange(cosmo::nn::MotionState status);

private:
    std::string atomic_code_;
    int max_width_{1920};
    int max_height_{1080};
    std::vector<TrackLabel> track_labels_;
    cosmo::nn::TrackerConfig track_config_;
    std::shared_ptr<cosmo::nn::TrackerWrap> tracker_;
};

using AiTrackerUnifyPtr = std::shared_ptr<AiTrackerUnify>;
}  // namespace cosmo
