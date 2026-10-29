#include "flow/alarm/AlarmVisualState.h"

#include <algorithm>

namespace cosmo::alarm {
bool RetainVisualTargets(DataAlarmUnit& unit, const std::vector<bool>& keep) {
    if (keep.empty() || std::all_of(keep.begin(), keep.end(), [](bool value) { return value; }))
        return true;
    if (std::none_of(keep.begin(), keep.end(), [](bool value) { return value; }))
        return false;
    if (keep.size() != unit.targets.size() || keep.size() != unit.visualCandidates.size())
        return true;
    std::vector<size_t> overlayIndices;
    std::vector<bool> used(unit.boxs.size(), false);
    for (size_t i = 0; i < keep.size(); ++i) {
        const auto& box    = unit.visualCandidates[i].box;
        const auto& target = unit.targets[i];
        if (box.x != target.box.x || box.y != target.box.y || box.width != target.box.width ||
            box.height != target.box.height || unit.visualCandidates[i].trackId != target.trackId)
            return true;
        const auto& candidateId = unit.visualCandidates[i].sourceTargetId;
        // Match immutable source identity; legacy producers may use a unique exact box.
        // Ambiguous annotation provenance must not change the existing filtering decision.
        size_t match   = unit.boxs.size();
        size_t matches = 0;
        for (size_t j = 0; j < unit.boxs.size(); ++j) {
            const auto& overlay = unit.boxs[j];
            const bool identity = !target.sourceTargetId.empty()
                                      ? overlay.sourceTargetId == target.sourceTargetId
                                      : overlay.sourceTargetId.empty();
            if (identity && overlay.box == box && candidateId == target.sourceTargetId) {
                match = j;
                ++matches;
            }
        }
        if (matches != 1 || used[match])
            match = unit.boxs.size();
        else
            used[match] = true;
        overlayIndices.push_back(match);
    }
    std::vector<CMsgOnEventsTarget> targets;
    std::vector<DataAlarmBox> overlays;
    for (size_t i = 0; i < keep.size(); ++i) {
        if (!keep[i])
            continue;
        const auto& target = unit.targets[i];
        const auto box     = unit.visualCandidates[i].box;
        if (targets.empty()) {
            unit.box        = box;
            unit.strTrackId = target.trackId;
            unit.trackId    = unit.visualCandidates[i].trackIndex;
        } else {
            const auto right  = std::max(unit.box.x + unit.box.width, box.x + box.width);
            const auto bottom = std::max(unit.box.y + unit.box.height, box.y + box.height);
            unit.box.x        = std::min(unit.box.x, box.x);
            unit.box.y        = std::min(unit.box.y, box.y);
            unit.box.width    = right - unit.box.x;
            unit.box.height   = bottom - unit.box.y;
        }
        targets.push_back(target);
        if (overlayIndices[i] != unit.boxs.size())
            overlays.push_back(unit.boxs[overlayIndices[i]]);
        else
            overlays.emplace_back(box, target.oriented_corners);
    }
    unit.targets     = std::move(targets);
    unit.boxs        = std::move(overlays);
    unit.haveRelated = false;
    return true;
}
}  // namespace cosmo::alarm
