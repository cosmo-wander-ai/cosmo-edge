#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "nn/device/axera/axera_route_identity.h"

namespace cosmo::person_template_contract {
struct Node {
    std::string id, parent, action, atomic, alarm_property, alarm_type;
    bool valid{true};
};
// 74470's four-stage pedestrian graph; UUIDs, frame rate and editable business
// settings are intentionally not identities. The output class is checked again
// against the loaded model and the active confidence at execution.
inline bool Matches(const std::vector<Node>& nodes, const std::string& category) {
    if (nodes.size() != 4 || (category != "2" && category != "3"))
        return false;
    const Node *source = nullptr, *detect = nullptr, *sensitivity = nullptr, *alarm = nullptr;
    for (const auto& n : nodes) {
        if (!n.valid || n.id.empty() || n.id == "-1")
            return false;
        for (const auto& other : nodes)
            if (&n != &other && n.id == other.id)
                return false;
        const Node** slot = nullptr;
        if (n.action == "BA_00001")
            slot = &source;
        else if (n.action == "AA_00001")
            slot = &detect;
        else if (n.action == "BA_00003")
            slot = &sensitivity;
        else if (n.action == "BA_00004")
            slot = &alarm;
        if (!slot || *slot)
            return false;
        *slot = &n;
    }
    return source && detect && sensitivity && alarm && source->parent == "-1" &&
           detect->parent == source->id && detect->atomic == "1001003" && sensitivity->parent == detect->id &&
           alarm->parent == sensitivity->id && alarm->alarm_property == "0" && alarm->alarm_type == "1";
}
inline bool CanReuseReference(bool graph_matches, const std::string& manifest,
                              const std::string& reference_scene, const std::string& model_hash,
                              const std::string& source_hash, const std::string& semantic) {
    return graph_matches && manifest == "1001003.json" && reference_scene == "74470" && !model_hash.empty() &&
           model_hash == cosmo::nn::axera::route_identity::Digest("person_720.sha256") &&
           !source_hash.empty() &&
           source_hash == cosmo::nn::axera::route_identity::Digest("person_720.source_onnx_sha256") &&
           semantic == "ax650-nv12-1080p-pitch2048-bt709-limited-person-bgr-pad140-nms-v1";
}
inline bool ValidThreshold(float value, float graph_min) {
    return std::isfinite(value) && std::isfinite(graph_min) && graph_min >= 0.f && value >= graph_min &&
           value <= 1.f;
}
}  // namespace cosmo::person_template_contract
