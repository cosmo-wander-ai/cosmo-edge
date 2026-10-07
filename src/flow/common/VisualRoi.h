#pragma once

#include "flow/common/AlgDataUnit.h"
#include "service/ai/IVisualDecisionService.h"

namespace cosmo {
struct VisualRoiInput {
    VideoFramePtr frame;
    util::Box roi;
    util::Box input_roi;
    bool is_det_box{false};
    std::string area_id;
    std::string roi_id;
    std::string source_target_id;
    std::string source_track_id;
    int source_track_index{-1};
    bool is_full_frame{false};
};

// Shared video/picture crop and host-pixel preparation. Typed mode retains
// invalid ROI identities and never substitutes full-frame pixels on failure.
std::vector<VisualRoiInput> PrepareVisualRois(const AlgData& data, const std::vector<MsgTaskArea>& areas,
                                              bool hasUpstreamTargets, bool typed);
nlohmann::json VisualRoiRecord(const VisualRoiInput& roi, const service::VisualDecisionResult& result,
                               const std::string& flowActionId);
}  // namespace cosmo
