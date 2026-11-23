#pragma once
// DTO bridge only. `model` and `scene_id` must come from independently
// authenticated, matching task/model contracts; passing this check does not
// prove those external identities or validate runtime tensor metadata.
#include <string>
#include <vector>

#include "flow/task/SleepSceneAdmission.h"
#include "util/dto/AlgorithmMsgTypes.h"
namespace cosmo::sleep_candidate {
#if defined(COSMO_AXERA_SLEEP_ROUTE_TEST_ONLY) || defined(COSMO_AXERA_SLEEP_ROUTE_PRODUCTION)
// Bound configuration for this exact converted model; source ONNX
// The labels/thresholds are from reference config; equality to the deployed
// reference-device weights and real-world accuracy remain unproven.
inline ModelContract BoundSleepReference() {
    return {"1001010",         "1001003",         "pedestrian", 1,     7,   0,    5,
            "pedSleepStomach", "pedSleepLieDown", .63f,         .954f, .9f, .98f, .55f};
}
#endif
inline bool AdmitSleepActionGraph(const std::string& scene_id, const std::vector<ActionNode>& actions,
                                  const ModelContract& model) noexcept {
    try {
        SceneContract c;
        c.scene_id                = scene_id;
        c.model                   = model;
        const ActionNode *tracker = nullptr, *logic = nullptr, *detector = nullptr, *classifier = nullptr;
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
            if (a.actionId == "AA_00001") {
                if (detector)
                    return false;
                detector = &a;
            }
            if (a.actionId == "AA_00002") {
                if (classifier)
                    return false;
                classifier = &a;
            }
        }
        if (!tracker || !logic || !detector || !classifier)
            return false;
        auto exactly = [](const ActionNode& n, const char* key) -> std::string {
            std::string v;
            int count = 0;
            for (const auto& x : n.configObject.params)
                if (x.key.ToRefString() == key) {
                    v = x.value.ToRefString();
                    ++count;
                }
            return count == 1 ? v : std::string{};
        };
        if (exactly(*detector, "atomicCode") != "1001003" || exactly(*classifier, "atomicCode") != "1001010")
            return false;
        c.track_select = exactly(*tracker, "trackSelect");
        if (exactly(*tracker, "motionStatus") != "2" || exactly(*tracker, "shapeChangeStatus") != "0")
            return false;
        c.motion_status  = 2;
        c.shape_status   = 0;
        const auto& root = logic->configObject.condition;
        if (!root.keyL.ToRefString().empty() || !root.keyR.ToRefString().empty())
            return false;
        c.condition_type = static_cast<int>(root.type);
        for (const auto& leaf : root.list) {
            if (!leaf.list.empty())
                return false;
            c.conditions.push_back(
                {static_cast<int>(leaf.type), leaf.keyL.ToRefString(), leaf.keyR.ToRefString()});
        }
        return AdmitSceneContract(c);
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::sleep_candidate
