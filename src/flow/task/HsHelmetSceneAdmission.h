#pragma once
// Private, disabled candidate. This predicate cannot establish a live model,
// native frame witness, queue generations, SDK status, or device ownership.
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "flow/classify/HsHelmetSceneContract.h"
namespace cosmo::hshelmet_candidate {
struct SceneNode {
    std::string action, flow, parent, atomic;
};
struct SceneContract {
    hshelmet_scene_candidate::Admission binding;
    std::string detector_label, track_select;
    int motion_status{-1}, shape_status{-1}, condition_type{-1};
    std::string condition_left, condition_right;
    float detector_threshold{-1.f}, classifier_threshold{-1.f};
    std::vector<SceneNode> nodes;
};
inline bool AdmitSceneContract(const SceneContract& c) noexcept {
    try {
        const auto* spec = hshelmet_scene_candidate::Lookup(c.binding.scene);
        if (!hshelmet_scene_candidate::ValidAdmission(c.binding) || !spec ||
            c.detector_label != "headShoulder" || c.track_select != "1001001" || c.motion_status != 0 ||
            c.shape_status != 0 || c.condition_type != 14 ||
            c.condition_left != "aiOut." + std::string(spec->output_label) + ".threshold" ||
            c.condition_right != "aiParam." + std::string(spec->output_label) + ".confidence" ||
            !hshelmet_scene_candidate::IsProbability(c.detector_threshold) ||
            !hshelmet_scene_candidate::IsProbability(c.classifier_threshold) || c.nodes.size() != 8)
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
        for (auto* node : ordered)
            if (!node)
                return false;
        if (ordered[0]->parent != "-1" || ordered[1]->atomic != "1001001" || ordered[4]->atomic != "2001002")
            return false;
        for (std::size_t i = 1; i < ordered.size(); ++i)
            if (ordered[i]->parent != ordered[i - 1]->flow)
                return false;
        return true;
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::hshelmet_candidate
