#pragma once
// Disabled-by-default bridge from the actual task DTO to the fall route's
// semantic contract. Queue links, exact model bytes and SDK metadata are
// separately checked during native route installation and worker start.
#include <vector>

#include "flow/task/FallSceneAdmission.h"
#include "util/dto/AlgorithmMsgTypes.h"
namespace cosmo::fall_candidate {
inline bool AdmitFallActionGraph(const std::vector<ActionNode>& actions) noexcept {
    try {
        SceneContract c;
        c.scene_id                = "6";
        c.detector_label          = "pedestrian";
        c.classifier_label        = "pedfalldown";
        c.classifier_index        = 1;
        c.detector_threshold      = .63f;
        c.classifier_threshold    = .8f;
        const ActionNode *tracker = nullptr, *logic = nullptr;
        for (const auto& a : actions) {
            c.nodes.push_back({a.actionId, a.flowActionId, a.preFlowActionId, a.atomicCode});
            if (a.actionId == "AA_00003") {
                if (tracker)
                    return false;
                tracker = &a;
            }
            if (a.actionId == "BA_90001") {
                if (logic)
                    return false;
                logic = &a;
            }
        }
        if (!tracker || !logic)
            return false;
        auto exactly = [&](const char* key) -> std::string {
            std::string value;
            int count = 0;
            for (const auto& x : tracker->configObject.params)
                if (x.key.ToRefString() == key) {
                    value = x.value.ToRefString();
                    ++count;
                }
            return count == 1 ? value : std::string{};
        };
        c.track_select    = exactly("trackSelect");
        const auto motion = exactly("motionStatus"), shape = exactly("shapeChangeStatus");
        if (motion != "2" || shape != "0")
            return false;
        c.motion_status   = 2;
        c.shape_status    = 0;
        const auto& op    = logic->configObject.condition;
        c.condition_type  = static_cast<int>(op.type);
        c.condition_left  = op.keyL.ToRefString();
        c.condition_right = op.keyR.ToRefString();
        if (!op.list.empty())
            return false;
        return AdmitSceneContract(c);
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::fall_candidate
