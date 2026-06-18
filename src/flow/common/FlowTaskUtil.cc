// Flow Task ID ↔ Channel+Algorithm ID conversion utilities.

#include "flow/common/FlowTaskUtil.h"

#include <algorithm>
#include <set>

#include "util/dto/ActionCodes.h"

namespace cosmo {

std::string ChannelAlgIdToTaskId(std::string_view channelId, std::string_view algId) {
    std::string result;
    result.reserve(channelId.size() + 1 + algId.size());
    result.append(channelId);
    result.push_back('_');
    result.append(algId);
    return result;
}

namespace {
    bool IsTargetSourceAction(std::string_view actionId) {
        return actionId == AADetect_Code || actionId == DADinoDetect_Code || actionId == AATrack_Code ||
               actionId == AAClassify_Code || actionId == AAClassifyAttr_Code ||
               actionId == AAClassifyArea_Code || actionId == AAClassifyGroup_Code ||
               actionId == AALandmark_Code || actionId == AAPersonFace_Code ||
               actionId == BAAssoTarget_Code || actionId == BATargetChooseBest_Code;
    }

    const ActionNode* FindActionByFlowId(const ActionAlgPtr& actionAlg, const std::string& flowActionId) {
        if (!actionAlg) {
            return nullptr;
        }
        auto it = std::find_if(actionAlg->workFlow.begin(), actionAlg->workFlow.end(),
                               [&](const auto& action) { return action.flowActionId == flowActionId; });
        return it == actionAlg->workFlow.end() ? nullptr : &(*it);
    }
}  // namespace

bool FlowHasUpstreamTargetSource(const ActionAlgPtr& actionAlg, const std::string& flowActionId) {
    const auto* current = FindActionByFlowId(actionAlg, flowActionId);
    std::set<std::string> visited;
    while (current && !current->preFlowActionId.empty() && current->preFlowActionId != "-1") {
        if (!visited.insert(current->flowActionId).second) {
            return false;
        }
        const auto* parent = FindActionByFlowId(actionAlg, current->preFlowActionId);
        if (!parent) {
            return false;
        }
        if (IsTargetSourceAction(parent->actionId)) {
            return true;
        }
        current = parent;
    }
    return false;
}

std::vector<AiDetectRstEl> BuildAreaFallbackTargets(const std::vector<MsgTaskArea>& areas, int width,
                                                    int height) {
    std::vector<AiDetectRstEl> targets;
    if (width <= 0 || height <= 0) {
        return targets;
    }

    for (auto area : areas) {
        if (area.pointBox.width <= 0.0 || area.pointBox.height <= 0.0) {
            AreaToLocalBox(area);
        }
        if (area.pointBox.width <= 0.0 || area.pointBox.height <= 0.0) {
            continue;
        }

        AiDetectRstEl target;
        target.box.x      = std::max(0, static_cast<int>(area.pointBox.x * width));
        target.box.y      = std::max(0, static_cast<int>(area.pointBox.y * height));
        target.box.width  = static_cast<int>(area.pointBox.width * width);
        target.box.height = static_cast<int>(area.pointBox.height * height);
        target.box.width  = std::min(width - target.box.x, target.box.width);
        target.box.height = std::min(height - target.box.y, target.box.height);
        if (target.box.width <= 0 || target.box.height <= 0) {
            continue;
        }

        TargetAreaUnit areaUnit;
        areaUnit.area_id   = area.areaId;
        areaUnit.area_name = area.name;
        target.areaSign.areas.push_back(areaUnit);
        targets.push_back(target);
    }
    return targets;
}

}  // namespace cosmo
