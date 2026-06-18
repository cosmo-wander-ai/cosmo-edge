// Flow Task ID ↔ Channel+Algorithm ID conversion utilities.

#pragma once

#include <vector>
#include <string>
#include <string_view>

#include "flow/common/AlgDetectTypes.h"
#include "flow/task/TaskBaseParam.h"
#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo {

// Compose a task ID from channel ID and algorithm ID (e.g. "CH001_ALG01").
std::string ChannelAlgIdToTaskId(std::string_view channelId, std::string_view algId);

bool FlowHasUpstreamTargetSource(const ActionAlgPtr& actionAlg, const std::string& flowActionId);

std::vector<AiDetectRstEl> BuildAreaFallbackTargets(const std::vector<MsgTaskArea>& areas, int width,
                                                    int height);

}  // namespace cosmo
