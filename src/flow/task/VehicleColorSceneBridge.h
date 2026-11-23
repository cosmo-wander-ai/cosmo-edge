#pragma once
// Converts task DTO graph facts into an explicit VehicleColor contract. Configuration
// label/channel, frozen model identity and resolved thresholds must be supplied
// from independently validated sources by the caller; DTO does not carry them.
#include <vector>

#include "flow/task/VehicleColorSceneAdmission.h"
#include "util/dto/AlgorithmMsgTypes.h"
namespace cosmo::vehiclecolor_candidate {
inline bool AdmitVehicleColorActionGraph(const std::vector<ActionNode>& actions,
                                         const vehiclecolor_scene_candidate::Admission& binding,
                                         float detector_threshold, float classifier_threshold) noexcept {
    try {
        SceneContract c{binding, "Vehicle", "", -1, -1, -1, "", "", detector_threshold, classifier_threshold,
                        {}};
        const ActionNode *detector = nullptr, *tracker = nullptr, *classifier = nullptr, *logic = nullptr;
        for (const auto& a : actions) {
            c.nodes.push_back({a.actionId, a.flowActionId, a.preFlowActionId, a.atomicCode});
            const ActionNode** slot = nullptr;
            if (a.actionId == "AA_00001")
                slot = &detector;
            else if (a.actionId == "AA_00003")
                slot = &tracker;
            else if (a.actionId == "AA_00002")
                slot = &classifier;
            else if (a.actionId == "BA_90001")
                slot = &logic;
            if (slot) {
                if (*slot)
                    return false;
                *slot = &a;
            }
        }
        if (!detector || !tracker || !classifier || !logic)
            return false;
        const auto exactly = [](const ActionNode& action, const char* key) -> std::string {
            std::string value;
            int count = 0;
            for (const auto& x : action.configObject.params)
                if (x.key.ToRefString() == key) {
                    value = x.value.ToRefString();
                    ++count;
                }
            return count == 1 ? value : std::string{};
        };
        if (exactly(*detector, "atomicCode") != "2000001" || exactly(*classifier, "atomicCode") != "2000011")
            return false;
        c.track_select = exactly(*tracker, "trackSelect");
        if (exactly(*tracker, "motionStatus") != "0" || exactly(*tracker, "shapeChangeStatus") != "0")
            return false;
        c.motion_status   = 0;
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
}  // namespace cosmo::vehiclecolor_candidate
