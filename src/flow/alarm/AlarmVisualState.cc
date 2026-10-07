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
    for (size_t i = 0; i < keep.size(); ++i) {
        const auto& box    = unit.visualCandidates[i].box;
        const auto& target = unit.targets[i];
        if (box.x != target.box.x || box.y != target.box.y || box.width != target.box.width ||
            box.height != target.box.height || unit.visualCandidates[i].trackId != target.trackId)
            return true;
    }
    std::vector<CMsgOnEventsTarget> targets;
    unit.boxs.clear();
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
        unit.boxs.emplace_back(box, target.oriented_corners);
    }
    unit.targets     = std::move(targets);
    unit.haveRelated = false;
    return true;
}
}  // namespace cosmo::alarm
