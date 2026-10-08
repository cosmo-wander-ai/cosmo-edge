#include <algorithm>
#include <chrono>
#include <map>
#include <set>

#include "flow/task/PTaskBase.h"
#include "util/StringUtil.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/PictureWorkflow.h"

namespace cosmo {
void DetTarget2MsgTarget(const AiDetectRstEl& target, MsgPTaskTarget& output);

namespace {

    std::vector<MsgDynamicKeyValue> EffectiveParams(const ActionNode& node,
                                                    const std::vector<MsgDynamicKeyValue>& defaults,
                                                    const MsgTaskConfig& task, const MsgTaskConfig& request,
                                                    const std::vector<MsgDynamicKeyValue>& scenario) {
        std::map<std::string, MsgDynamicKeyValue> merged;
        for (const auto* list :
             {&defaults, &node.configObject.params, &scenario, &task.params, &request.params})
            for (const auto& param : *list) {
                // Model identity belongs to the compiled plan, never a request override.
                if (param.key == "atomicCode" && list != &node.configObject.params)
                    continue;
                merged[param.key.ToString()] = param;
            }
        std::vector<MsgDynamicKeyValue> result;
        for (auto& [key, param] : merged) {
            const auto parts = util::Split(key, ".");
            param.keys.assign(parts.begin(), parts.end());
            result.push_back(param);
        }
        return result;
    }

    size_t TargetCount(const AlgDataPtr& data) {
        return data && data->chanDataDetect.detRet ? data->chanDataDetect.detRet->targets.size() : 0;
    }

    std::string Param(const std::vector<MsgDynamicKeyValue>& params, const std::string& key,
                      const std::string& fallback) {
        for (const auto& param : params)
            if (param.key == key)
                return param.value.ToString();
        return fallback;
    }

    MsgPTaskTarget MakeTarget(const AiDetectRstEl& target, const AlgData& data, bool details) {
        MsgPTaskTarget output;
        output.bHaveLogicResult = data.bHaveLogic;
        DetTarget2MsgTarget(target, output);
        if (details) {
            output.targetId     = target.targetId;
            output.sourceNodeId = target.targetId.substr(0, target.targetId.rfind(':'));
            output.atomicCode   = target.algCode;
            output.filtered     = target.bFilter;
            output.filterReason = target.filterDesc;
            const auto result   = data.pictureDecisions.find(target.targetId);
            output.decision     = result == data.pictureDecisions.end() ? "not_evaluated" : result->second;
            for (const auto& [id, rules] : data.pictureRules) {
                const auto rule = rules.find(target.targetId);
                if (rule != rules.end())
                    output.rules[id] = rule->second;
            }
        }
        return output;
    }

}  // namespace

util::ErrorEnum PTaskBase::ExecutePicture(PTaskElementPtr task, AlgDataPtr input,
                                          const MsgPTaskDetectPicRecv& request,
                                          MsgPTaskDetectPicSend& response, AlgDataPtr& rendered) {
    if (!task || !input)
        return util::ErrorEnum::FlowDataInvalid;
    if (request.resultMode != "legacy" && request.resultMode != "business" && request.resultMode != "debug")
        return util::ErrorEnum::InvalidParam;
    if (!request.taskConfig.areas.empty() || !request.taskConfig.shieldedAreas.empty())
        return util::ErrorEnum::InvalidParam;
    const bool business  = request.resultMode != "legacy";
    auto& result         = response.resData;
    result.schemaVersion = business ? 2 : 1;
    result.requestId     = request.requestId.empty() ? util::GenerateUUID() : request.requestId;
    result.status        = "completed";
    result.errorNodeId.clear();
    result.outputs.clear();
    result.targetList.clear();
    result.areaList.clear();
    result.nodes.clear();
    input->taskId = task->taskId;
    std::map<std::string, AlgDataPtr> frames;
    std::set<std::string> parents;
    for (const auto& node : task->actions)
        parents.insert(node.action.preFlowActionId);
    std::map<std::string, size_t> returned;
    std::set<std::string> renderedIds;
    rendered                        = AlgDataCopy(input);
    rendered->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    for (auto& entry : task->actions) {
        const auto& node  = entry.action;
        const auto parent = frames.find(node.preFlowActionId);
        auto data         = AlgDataCopy(parent == frames.end() ? input : parent->second);
        const auto params = EffectiveParams(
            node, entry.modelParams, task->params, request.taskConfig,
            task->action_alg ? task->action_alg->pictureDefaults : std::vector<MsgDynamicKeyValue>{});
        MsgPTaskDetectPicSend::NodeResult trace;
        trace.nodeId           = node.flowActionId;
        trace.actionId         = node.actionId;
        trace.inputCount       = TargetCount(data);
        const auto start       = std::chrono::steady_clock::now();
        auto mutable_params    = params;
        util::ErrorEnum status = util::ErrorEnum::Success;
        if (!data->pictureBranch) {
            trace.status = "skipped";
        } else if (!ValidatePictureParams(params) ||
                   !entry.actionInst->SetParam(task->taskId, mutable_params)) {
            status = util::ErrorEnum::InvalidParam;
        } else {
            std::set<std::string> upstreamIds;
            if (data->chanDataDetect.detRet)
                for (const auto& target : data->chanDataDetect.detRet->targets)
                    if (!target.targetId.empty())
                        upstreamIds.insert(target.targetId);
            // Filtered targets never reach an expensive downstream model. Keep them for debug output.
            std::vector<AiDetectRstEl> filtered;
            if (data->chanDataDetect.detRet && node.actionId != PAFilter_Code) {
                auto& targets = data->chanDataDetect.detRet->targets;
                for (const auto& target : targets)
                    if (target.bFilter)
                        filtered.push_back(target);
                targets.erase(std::remove_if(targets.begin(), targets.end(),
                                             [](const auto& target) { return target.bFilter; }),
                              targets.end());
            }
            status = entry.actionInst->HandPic(data);
            if (data->chanDataDetect.detRet) {
                auto& targets = data->chanDataDetect.detRet->targets;
                for (size_t i = 0; i < targets.size(); ++i) {
                    // Model postprocessors may generate UUIDs. Assign plan-local identities to new
                    // targets while keeping identities of targets enriched by downstream nodes.
                    if (targets[i].targetId.empty() || !upstreamIds.count(targets[i].targetId))
                        targets[i].targetId = node.flowActionId + ":" + std::to_string(i);
                    if (targets[i].algCode.empty())
                        targets[i].algCode = entry.actionInst->GetAtomicCode();
                }
                targets.insert(targets.end(), filtered.begin(), filtered.end());
            }
            trace.status = "completed";
        }
        trace.outputCount = TargetCount(data);
        trace.durationMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (status != util::ErrorEnum::Success) {
            trace.status       = "failed";
            result.status      = "failed";
            result.errorNodeId = node.flowActionId;
            result.outputs.clear();
            result.targetList.clear();
            result.nodes.push_back(trace);
            return status;
        }
        if (request.resultMode == "debug")
            result.nodes.push_back(trace);
        frames[node.flowActionId] = data;
        if (node.actionId != PAOutput_Code && parents.count(node.flowActionId))
            continue;

        MsgPTaskDetectPicSend::Output output;
        output.nodeId           = node.flowActionId;
        output.name             = Param(params, "output.name", task->GetAlgName());
        const auto selection    = Param(params, "output.targets", "matched");
        bool unknown            = false;
        bool onlyMissingSamples = true;
        if (data->pictureBranch && data->chanDataDetect.detRet) {
            for (const auto& target : data->chanDataDetect.detRet->targets) {
                auto decision         = data->pictureDecisions.find(target.targetId);
                const bool is_unknown = data->pictureDecision == "unknown" ||
                                        (data->bHaveLogic && (decision == data->pictureDecisions.end() ||
                                                              decision->second == "unknown"));
                const bool matched = !target.bFilter && !is_unknown &&
                                     data->pictureDecision != "not_matched" &&
                                     (!data->bHaveLogic || target.bLogicResult);
                if (!target.bFilter && is_unknown) {
                    unknown            = true;
                    onlyMissingSamples = onlyMissingSamples && target.matchInfo.setPicCount == 0;
                }
                if (matched)
                    ++output.matchedCount;
                const bool selected = !target.bFilter && (selection == "all" || matched);
                if (selected)
                    output.targetIds.push_back(target.targetId);
                // Library evidence is useful even when an inverted business rule does not fire.
                // Keep output targetIds and matchedCount restricted to the business selection.
                const bool libraryMatch = !target.bFilter && target.matchInfo.matched;
                if (selected || libraryMatch || request.resultMode == "debug" ||
                    (!business && !target.bFilter)) {
                    auto msg      = MakeTarget(target, *data, business);
                    auto existing = returned.find(target.targetId);
                    if (existing == returned.end()) {
                        returned[target.targetId] = result.targetList.size();
                        result.targetList.push_back(std::move(msg));
                    } else {
                        // Forks may add different rule results to the same original target.
                        auto& saved = result.targetList[existing->second];
                        saved.rules.insert(msg.rules.begin(), msg.rules.end());
                        for (const auto& value : msg.confidence)
                            if (std::none_of(saved.confidence.begin(), saved.confidence.end(),
                                             [&](const auto& old) { return old.label == value.label; }))
                                saved.confidence.push_back(value);
                        for (const auto& value : msg.texts)
                            if (std::find(saved.texts.begin(), saved.texts.end(), value) == saved.texts.end())
                                saved.texts.push_back(value);
                        if (!msg.attributes.empty())
                            saved.attributes = msg.attributes;
                        if (!msg.landmark.empty())
                            saved.landmark = msg.landmark;
                        if (!msg.maskPolygon.empty())
                            saved.maskPolygon = msg.maskPolygon;
                        if (msg.bHaveMatchInfo) {
                            saved.matchInfo      = msg.matchInfo;
                            saved.bHaveMatchInfo = true;
                        }
                    }
                }
                if (selected && renderedIds.insert(target.targetId).second)
                    rendered->chanDataDetect.detRet->targets.push_back(target);
            }
        }
        output.decision = output.matchedCount ? "matched" : (unknown ? "unknown" : "not_matched");
        if (!data->pictureDecision.empty())
            output.decision = data->pictureDecision;
        if (!data->pictureBranch && output.decision != "unknown")
            output.decision = "not_matched";
        if (output.decision == "unknown")
            output.reason = unknown && onlyMissingSamples ? "no_comparable_samples" : "insufficient_evidence";
        result.outputs.push_back(std::move(output));
    }
    if (!business) {
        MsgPTaskArea legacy;
        legacy.areaId     = "-1";
        legacy.areaName   = "default";
        legacy.targetList = result.targetList;
        legacy.bDetected  = std::any_of(result.outputs.begin(), result.outputs.end(),
                                        [](const auto& output) { return output.decision == "matched"; });
        result.areaList.push_back(std::move(legacy));
    }
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
