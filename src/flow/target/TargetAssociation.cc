#include "flow/target/TargetAssociation.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "util/SafeParse.h"

namespace cosmo {
namespace {
    bool EligibleParent(const AiDetectRstEl& parent) {
        return parent.trackId >= 0 && !parent.trackIdInfo.empty() && !parent.bFilter &&
               parent.trackStatus != AITrackingStatus::LOSS && parent.box.width > 0 && parent.box.height > 0;
    }

    bool IsCandidate(const AiDetectRstEl& parent, const AiDetectRstEl& child,
                     const TargetAssociationConfig& config) {
        const auto& body = parent.box;
        const auto& box  = child.box;
        if (child.bFilter || box.width <= 0 || box.height <= 0 ||
            !std::isfinite(child.confidence.confidence) || child.confidence.confidence < config.confidence ||
            std::find(config.labels.begin(), config.labels.end(), child.confidence.label) ==
                config.labels.end()) {
            return false;
        }
        const double right    = static_cast<double>(body.x) + body.width;
        const double bottom   = static_cast<double>(body.y) + body.height;
        const double center_x = static_cast<double>(box.x) + box.width * 0.5;
        const double center_y = static_cast<double>(box.y) + box.height * 0.5;
        const double top_limit =
            config.region == AssociationRegion::kLowerHalf ? body.y + body.height * 0.5 : body.y;
        const double bottom_limit =
            config.region == AssociationRegion::kUpperHalf ? body.y + body.height * 0.5 : bottom;
        if (center_x < body.x || center_x > right || center_y < top_limit || center_y > bottom_limit) {
            return false;
        }
        const double width =
            std::max(0.0, std::min(right, static_cast<double>(box.x) + box.width) - std::max(body.x, box.x));
        const double height = std::max(
            0.0, std::min(bottom, static_cast<double>(box.y) + box.height) - std::max(body.y, box.y));
        return width * height >= config.min_containment * static_cast<double>(box.width) * box.height;
    }
}  // namespace

bool UpdateTargetAssociationConfig(TargetAssociationConfig& config,
                                   const std::vector<MsgDynamicKeyValue>& params) {
    auto next             = config;
    const auto find_value = [&](const std::string& key) -> const std::string* {
        const auto it = std::find_if(params.rbegin(), params.rend(),
                                     [&](const auto& param) { return param.key.ToString() == key; });
        return it == params.rend() ? nullptr : &it->value.ToRefString();
    };
    const auto* size = find_value("param.minTargetSize");
    if (!size)
        size = find_value("param.minFaceSize");
    if (size) {
        next.min_size = util::ParseInt(*size, -1);
        if (next.min_size < 1 || next.min_size > 1000)
            return false;
    }
    const auto* confidence = find_value("param.detectionConfidence");
    if (!confidence)
        confidence = find_value("param.faceDetectionConfidence");
    if (confidence) {
        next.confidence = util::ParseFloat(*confidence, -1.0f);
        if (!std::isfinite(next.confidence) || next.confidence < 0 || next.confidence > 1)
            return false;
    }
    if (const auto* value = find_value("param.minContainment")) {
        next.min_containment = util::ParseDouble(*value, -1.0);
        if (!std::isfinite(next.min_containment) || next.min_containment < 0 || next.min_containment > 1)
            return false;
    }
    if (const auto* value = find_value("param.associationRegion")) {
        if (*value == "whole")
            next.region = AssociationRegion::kWhole;
        else if (*value == "upper")
            next.region = AssociationRegion::kUpperHalf;
        else if (*value == "lower")
            next.region = AssociationRegion::kLowerHalf;
        else
            return false;
    }
    if (const auto* value = find_value("param.associationLabels")) {
        const auto labels = nlohmann::json::parse(*value, nullptr, false);
        if (!labels.is_array())
            return false;
        next.labels.clear();
        for (const auto& label : labels) {
            if (!label.is_string() || label.get<std::string>().empty())
                return false;
            const auto text = label.get<std::string>();
            if (std::find(next.labels.begin(), next.labels.end(), text) == next.labels.end()) {
                next.labels.push_back(text);
            }
        }
    }
    config = std::move(next);
    return true;
}

void AssociateTargets(std::vector<AiDetectRstEl>& parents, const std::vector<AiDetectRstEl>& children,
                      const TargetAssociationConfig& config, bool observation_complete) {
    std::vector<std::vector<size_t>> candidates(parents.size());
    std::vector<size_t> owners(children.size(), 0);
    for (size_t p = 0; p < parents.size(); ++p) {
        auto& parent     = parents[p];
        parent.relatedEl = {};
        parent.relatedEls.clear();
        const bool eligible       = EligibleParent(parent);
        parent.association_status = !eligible ? TargetAssociationStatus::kFiltered
                                    : (!observation_complete || config.labels.empty())
                                        ? TargetAssociationStatus::kUnavailable
                                        : TargetAssociationStatus::kMissing;
        if (parent.association_status != TargetAssociationStatus::kMissing)
            continue;
        for (size_t c = 0; c < children.size(); ++c) {
            if (IsCandidate(parent, children[c], config)) {
                candidates[p].push_back(c);
                ++owners[c];
            }
        }
    }
    for (size_t p = 0; p < parents.size(); ++p) {
        auto& parent = parents[p];
        if (candidates[p].size() == 1 && owners[candidates[p].front()] == 1) {
            const auto& child           = children[candidates[p].front()];
            parent.relatedEl.bActive    = true;
            parent.relatedEl.box        = child.box;
            parent.relatedEl.confidence = child.confidence;
            // Some detectors provide landmarks together with the detection (e.g. plates).
            parent.relatedEl.landmark = child.landmark;
            parent.association_status =
                child.box.width < config.min_size || child.box.height < config.min_size
                    ? TargetAssociationStatus::kTooSmall
                    : TargetAssociationStatus::kMatched;
        } else if (!candidates[p].empty()) {
            parent.association_status = TargetAssociationStatus::kAmbiguous;
        }
        if (parent.association_status != TargetAssociationStatus::kMatched && !parent.bFilter) {
            parent.bFilter    = true;
            parent.filterType = parent.association_status == TargetAssociationStatus::kTooSmall
                                    ? AIFilterType::TargetFilter
                                    : AIFilterType::NoRelatedTarget;
            parent.filterDesc = parent.association_status == TargetAssociationStatus::kTooSmall
                                    ? "Related target below minimum size"
                                    : "No usable unambiguous related target";
        }
    }
}

}  // namespace cosmo
