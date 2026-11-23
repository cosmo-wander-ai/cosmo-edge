#pragma once
// Isolated admission predicate: not yet connected to TaskBase or any route.
// Model hash/metadata, permits, queue generations and device state are separate gates.
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>
namespace cosmo::fall_candidate {
struct SceneNode {
    std::string action, flow, parent, atomic;
};
struct SceneContract {
    std::string scene_id, detector_label, classifier_label, track_select;
    int classifier_index{-1}, motion_status{-1}, shape_status{-1}, condition_type{-1};
    std::string condition_left, condition_right;
    float detector_threshold{-1.f}, classifier_threshold{-1.f};
    std::vector<SceneNode> nodes;
};
inline bool AdmitSceneContract(const SceneContract& c) noexcept {
    try {
        if (c.scene_id != "6" || c.detector_label != "pedestrian" || c.classifier_label != "pedfalldown" ||
            c.classifier_index != 1 || c.track_select != "1001003" || c.motion_status != 2 ||
            c.shape_status != 0 || c.condition_type != 14 ||
            c.condition_left != "aiOut.pedfalldown.threshold" ||
            c.condition_right != "aiParam.pedfalldown.confidence" || !std::isfinite(c.detector_threshold) ||
            c.detector_threshold < .1f || c.detector_threshold > 1.f ||
            !std::isfinite(c.classifier_threshold) || c.classifier_threshold < 0.f ||
            c.classifier_threshold > 1.f || c.nodes.size() != 8)
            return false;
        const std::array<std::string, 8> actions{"BA_00001", "AA_00001", "AA_00003", "BA_00002",
                                                 "AA_00002", "BA_90001", "BA_00003", "BA_00004"};
        std::array<const SceneNode*, 8> ordered{};
        std::set<std::string> flows;
        for (const auto& n : c.nodes) {
            if (n.flow.empty() || n.flow == "-1" || !flows.insert(n.flow).second)
                return false;
            bool found = false;
            for (std::size_t i = 0; i < actions.size(); ++i)
                if (n.action == actions[i]) {
                    if (ordered[i])
                        return false;
                    ordered[i] = &n;
                    found      = true;
                    break;
                }
            if (!found)
                return false;
        }
        for (auto n : ordered)
            if (!n)
                return false;
        if (ordered[0]->parent != "-1" || ordered[1]->atomic != "1001003" || ordered[4]->atomic != "2002008")
            return false;
        for (std::size_t i = 1; i < ordered.size(); ++i)
            if (ordered[i]->parent != ordered[i - 1]->flow)
                return false;
        return true;
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::fall_candidate
