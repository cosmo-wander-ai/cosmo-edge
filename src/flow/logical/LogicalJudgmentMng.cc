// LogicalJudgmentMng — Logical Judgment Mng implementation.

#include "flow/logical/LogicalJudgmentMng.h"

#include "util/Log.h"

namespace cosmo {

LogicalJudgmentPtr LogicalJudgmentMng::FindUniqueByTaskId(const std::string& taskId, const char* caller) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    LogicalJudgmentPtr matched;
    int matchCount = 0;
    for (const auto& inst : insts) {
        if (inst && inst->GetTaskId() == taskId) {
            matched = inst;
            ++matchCount;
        }
    }
    if (matchCount > 1) {
        LOG_WARN("{} Task:{} has {} LogicalJudgment instances. Refuse task-only lookup.", caller, taskId,
                 matchCount);
        return nullptr;
    }
    return matched;
}

bool LogicalJudgmentMng::RegistTaskQueue(AlgTaskUnit& param) {
    auto inst = FindUniqueByTaskId(param.task_id, "RegistTaskQueue");
    if (inst) {
        return inst->RegistTaskQueue(param);
    }
    return false;
}

bool LogicalJudgmentMng::RemoveTaskQueue(AlgTaskUnit& param) {
    auto inst = FindUniqueByTaskId(param.task_id, "RemoveTaskQueue");
    if (inst) {
        return inst->RemoveTaskQueue(param);
    }
    return false;
}

bool LogicalJudgmentMng::LogicTest(const std::string& taskId, const MsgTarget& msgTarget) {
    auto inst = FindUniqueByTaskId(taskId, "LogicTest");
    if (!inst) {
        LOG_WARN("{} Have Not Get Inst ", taskId);
        return false;
    }

    AiDetectRstEl target;
    target.trackId    = msgTarget.trackId;
    target.box.x      = msgTarget.aiBox.x;
    target.box.y      = msgTarget.aiBox.y;
    target.box.width  = msgTarget.aiBox.width;
    target.box.height = msgTarget.aiBox.height;
    for (auto& msgConfidence : msgTarget.confidence) {
        AiConfidence confidence;
        confidence.confidence = msgConfidence.confidence;
        confidence.label      = msgConfidence.label;
        target.classifyRst.push_back(confidence);
    }

    return inst->LogicTest(target);
}
}  // namespace cosmo
