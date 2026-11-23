#pragma once
// Static scene admission only. The caller must separately authenticate the
// source of this contract, the exact model bytes and runtime SDK metadata.
// This header does not start a route or grant authority to use a device.
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>
namespace cosmo::sleep_candidate {
struct SceneNode {
    std::string action, flow, parent, atomic;
};
struct ConditionLeaf {
    int type{-1};
    std::string left, right;
};
struct ModelContract {
    std::string atomic, detector_atomic, detector_label;
    int output_batch{-1}, output_width{-1}, stomach_index{-1}, lie_down_index{-1};
    std::string stomach_label, lie_down_label;
    float detector_threshold{-1.f}, stomach_strict{-1.f}, stomach_recommended{-1.f};
    float lie_down_strict{-1.f}, lie_down_recommended{-1.f};
};
struct SceneContract {
    std::string scene_id, track_select;
    int motion_status{-1}, shape_status{-1}, condition_type{-1};
    std::vector<ConditionLeaf> conditions;
    std::vector<SceneNode> nodes;
    ModelContract model;
};
inline bool AdmitSceneContract(const SceneContract& c) noexcept {
    try {
        const auto& m = c.model;
        if (c.scene_id != "65" || c.track_select != "1001003" || c.motion_status != 2 ||
            c.shape_status != 0 || c.condition_type != 1 || c.conditions.size() != 2 || c.nodes.size() != 8 ||
            m.atomic != "1001010" || m.detector_atomic != "1001003" || m.detector_label != "pedestrian" ||
            m.output_batch != 1 || m.output_width != 7 || m.stomach_index != 0 || m.lie_down_index != 5 ||
            m.stomach_label != "pedSleepStomach" || m.lie_down_label != "pedSleepLieDown" ||
            !std::isfinite(m.detector_threshold) || !std::isfinite(m.stomach_strict) ||
            !std::isfinite(m.stomach_recommended) || !std::isfinite(m.lie_down_strict) ||
            !std::isfinite(m.lie_down_recommended) || m.detector_threshold != .63f ||
            m.stomach_strict != .954f || m.stomach_recommended != .9f || m.lie_down_strict != .98f ||
            m.lie_down_recommended != .55f)
            return false;
        bool stomach = false, lie = false;
        for (const auto& leaf : c.conditions) {
            if (leaf.type != 14)
                return false;
            if (leaf.left == "aiOut.pedSleepStomach.threshold" &&
                leaf.right == "aiParam.pedSleepStomach.confidence") {
                if (stomach)
                    return false;
                stomach = true;
            } else if (leaf.left == "aiOut.pedSleepLieDown.threshold" &&
                       leaf.right == "aiParam.pedSleepLieDown.confidence") {
                if (lie)
                    return false;
                lie = true;
            } else
                return false;
        }
        if (!stomach || !lie)
            return false;
        const std::array<std::string, 8> kinds{"BA_00001", "AA_00001", "AA_00003", "BA_00002",
                                               "AA_00002", "BA_90001", "BA_00003", "BA_00004"};
        std::array<const SceneNode*, 8> ordered{};
        std::set<std::string> flows;
        for (const auto& n : c.nodes) {
            if (n.flow.empty() || n.flow == "-1" || !flows.insert(n.flow).second)
                return false;
            bool found = false;
            for (std::size_t i = 0; i < kinds.size(); ++i)
                if (n.action == kinds[i]) {
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
        if (ordered[0]->parent != "-1" || ordered[1]->atomic != "1001003" || ordered[4]->atomic != "1001010")
            return false;
        for (std::size_t i = 1; i < ordered.size(); ++i)
            if (ordered[i]->parent != ordered[i - 1]->flow)
                return false;
        return true;
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::sleep_candidate
