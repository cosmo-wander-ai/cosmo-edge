// Base template class for ActionMng, eliminating duplicate logic like
// QueueStatus/ActionInfo/DeleteInst across Mng classes

#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "util/Log.h"
#include "util/dto/AlgDataQueueTypes.h"

namespace cosmo {

// ──────────────────────────────────────────────────────
// IMngStatusProvider: Unified interface for QueueStatus/ActionInfo
// TaskBase uses this interface to iterate through all Mng instances
// ──────────────────────────────────────────────────────
struct IMngStatusProvider {
    virtual ~IMngStatusProvider() = default;
    virtual void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) = 0;
    virtual void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos)                                 = 0;
};

// Adapter: Wraps any class with QueueStatus/ActionInfo methods as IMngStatusProvider
// Used for MapActionMng derived classes (they do not inherit IMngStatusProvider directly because some InstT
// lack these methods)
template <typename T>
class MngStatusAdapter : public IMngStatusProvider {
public:
    explicit MngStatusAdapter(T& mng) : mng_(mng) {}
    void QueueStatus(std::vector<AlgActionDataQueueStatus>& s, unsigned int d) override {
        mng_.QueueStatus(s, d);
    }
    void ActionInfo(std::vector<ActionRuntimeInfo>& i) override {
        mng_.ActionInfo(i);
    }

private:
    T& mng_;
};

// ──────────────────────────────────────────────────────
// VectorActionMng: Stores instances using vector<shared_ptr<T>>
// Applicable to: ActionBranchMng, LogicalJudgmentMng, SensitivityMng,
//         PosSaveSensitivityMng, TaskAlarmMng, AreaAlarmMng, TargetFilterMng
// ──────────────────────────────────────────────────────
template <typename InstT>
class VectorActionMng : public IMngStatusProvider {
public:
    using InstPtr = std::shared_ptr<InstT>;

    explicit VectorActionMng(const char* name) : mng_name(name) {
        LOG_INFO("{} Init", mng_name);
    }

    virtual ~VectorActionMng() {
        LOG_INFO("{} Delete", mng_name);
    }

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus,
                     unsigned int durationSec = 30) override {
        std::shared_lock<std::shared_mutex> lock(mtx);
        for (auto& inst : insts) {
            inst->QueueStatus(queStatus, durationSec);
        }
    }

    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) override {
        std::shared_lock<std::shared_mutex> lock(mtx);
        for (auto& inst : insts) {
            inst->ActionInfo(actionInfos);
        }
    }

    bool DeleteInst(InstPtr inst) {
        if (!inst) {
            return false;
        }
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = std::find_if(insts.begin(), insts.end(),
                               [&](const InstPtr& item) { return MatchForDelete(item, inst); });
        if (it != insts.end()) {
            insts.erase(it);
            return true;
        }
        return false;
    }

    // Generic GetInst: Find by taskId + flowActionId, create if not found
    // Requires InstT constructor: (taskId, ActionNode&)
    InstPtr GetInst(const std::string& taskId, ActionNode& action) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto inst = FindByTaskAndFlowUnlocked(taskId, action.flowActionId);
        if (inst) {
            return inst;
        }
        auto newInst = std::make_shared<InstT>(taskId, action);
        insts.push_back(newInst);
        return newInst;
    }

protected:
    // Match and delete by taskId by default, subclass can override (e.g., match flowActionId simultaneously)
    virtual bool MatchForDelete(const InstPtr& existing, const InstPtr& target) const {
        return existing->GetTaskId() == target->GetTaskId();
    }

    InstPtr FindByTaskId(const std::string& taskId) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        return FindByTaskIdUnlocked(taskId);
    }

    InstPtr FindByTaskIdUnlocked(const std::string& taskId) {
        auto it = std::find_if(insts.begin(), insts.end(),
                               [&](const InstPtr& inst) { return inst->GetTaskId() == taskId; });
        return (it != insts.end()) ? *it : nullptr;
    }

    // Find existing instance in vector by taskId + flowActionId
    InstPtr FindByTaskAndFlow(const std::string& taskId, const std::string& flowActionId) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        return FindByTaskAndFlowUnlocked(taskId, flowActionId);
    }

    InstPtr FindByTaskAndFlowUnlocked(const std::string& taskId, const std::string& flowActionId) {
        auto it = std::find_if(insts.begin(), insts.end(), [&](const InstPtr& inst) {
            return (inst->GetTaskId() == taskId) && (inst->GetFlowActionId() == flowActionId);
        });
        return (it != insts.end()) ? *it : nullptr;
    }

    std::shared_mutex mtx;
    std::vector<InstPtr> insts;
    std::string mng_name;
};

// ──────────────────────────────────────────────────────
// MapActionMng: Stores instances using map<string, shared_ptr<T>>
// Applicable to: AiClassifyMng, AiClassifyGroupMng, AiClassifyAreaMng,
//         AiClassifyAttrMng, AiTrackMng, AiLandmarkMng,
//         AiVideoQualityMng, TargetChooseBestMng,
//         PClassifierMng, PDetectorMng, PLogicalJudgmentMng
// ──────────────────────────────────────────────────────
template <typename InstT>
class MapActionMng {
public:
    using InstPtr = std::shared_ptr<InstT>;

    explicit MapActionMng(const char* name) : mng_name(name) {
        LOG_INFO("{} Init", mng_name);
    }

    virtual ~MapActionMng() {
        LOG_INFO("{} Delete", mng_name);
    }

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec = 30) {
        std::shared_lock<std::shared_mutex> lock(mtx);
        for (auto it = inst_map.begin(); it != inst_map.end(); it++) {
            it->second->QueueStatus(queStatus, durationSec);
        }
    }

    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) {
        std::shared_lock<std::shared_mutex> lock(mtx);
        for (auto it = inst_map.begin(); it != inst_map.end(); it++) {
            it->second->ActionInfo(actionInfos);
        }
    }

protected:
    std::shared_mutex mtx;
    std::map<std::string, InstPtr> inst_map;
    std::string mng_name;
};

// ──────────────────────────────────────────────────────
// SimpleMapActionMng: Further simplified MapActionMng
// Applicable to classes with exactly identical GetInst(task, action) / DeleteInst(inst) patterns
// Uses task + flowActionId as key
// ──────────────────────────────────────────────────────
template <typename InstT>
class SimpleMapActionMng : public MapActionMng<InstT> {
public:
    using InstPtr = typename MapActionMng<InstT>::InstPtr;
    using MapActionMng<InstT>::mtx;
    using MapActionMng<InstT>::inst_map;
    using MapActionMng<InstT>::mng_name;

    explicit SimpleMapActionMng(const char* name) : MapActionMng<InstT>(name) {}

    bool DeleteInst(InstPtr inst) {
        if (!inst) {
            return false;
        }
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto key = MakeDeleteKey(inst);
        inst_map.erase(key);
        LOG_INFO("[{}] Delete Task:{} key:{}. Current active instances: {}", mng_name, inst->GetTaskId(), key,
                 inst_map.size());
        return true;
    }

    // Generic GetInst: Find by taskId + flowActionId, create if not found
    // Requires InstT constructor: (taskId, ActionNode&)
    InstPtr GetInst(const std::string& taskId, ActionNode& action) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto key  = taskId + action.flowActionId;
        auto inst = inst_map[key];
        if (inst) {
            return inst;
        }
        LOG_INFO("[{}] Add:{} flowActionId:{}", mng_name, taskId, action.flowActionId);
        auto newInst  = std::make_shared<InstT>(taskId, action);
        inst_map[key] = newInst;
        LOG_INFO("[{}] Current active instances: {}", mng_name, inst_map.size());
        return newInst;
    }

protected:
    // Generate key by taskId + flowActionId by default, subclass can override
    virtual std::string MakeDeleteKey(const InstPtr& inst) const {
        return inst->GetTaskId() + inst->GetFlowActionId();
    }
};

// ──────────────────────────────────────────────────────
// MultiChannelActionMng: Mng for multi-channel shared instances
// Stored using map<algCode, vector<InstPtr>>
// Applicable to: AiDetectMng, DinoDetectMng, Qwen3VLMng, Sam2SegmentMng
// InstT needs to provide: ChannelExist, AddTask, RemoveTask,
//                 TaskIsFull, TaskIsEmpty, GetAlgCode, ModifyParam
// ──────────────────────────────────────────────────────
template <typename InstT>
class MultiChannelActionMng : public IMngStatusProvider {
public:
    using InstPtr = std::shared_ptr<InstT>;

    explicit MultiChannelActionMng(const char* name) : mng_name(name) {
        LOG_INFO("{} Init", mng_name);
    }

    virtual ~MultiChannelActionMng() {
        LOG_INFO("{} Delete", mng_name);
    }

    InstPtr GetInst(const std::string& algCode, const std::string& channelId, const std::string& task,
                    ActionNode& action) {
        InstPtr canBeAddInst = nullptr;
        std::lock_guard<std::shared_mutex> lock(m_mtx);
        auto& algInsts = m_insts[algCode];

        for (const auto& inst : algInsts) {
            if (inst->ChannelExist(channelId)) {
                if (inst->AddTask(channelId, task)) {
                    if (!action.configObject.params.empty())
                        inst->ModifyParam(channelId, task, action.configObject.params);
                    return inst;
                }
                LOG_WARN("[Channel:{} Task:{}] Get {} Inst Failed", channelId, task, algCode);
                return nullptr;
            } else {
                if ((!canBeAddInst) && (!inst->TaskIsFull())) {
                    canBeAddInst = inst;
                }
            }
        }

        if (canBeAddInst) {
            if (canBeAddInst->AddTask(channelId, task)) {
                if (!action.configObject.params.empty())
                    canBeAddInst->ModifyParam(channelId, task, action.configObject.params);
                return canBeAddInst;
            }
            LOG_WARN("[Channel:{} Task:{}] Get {} Inst Failed", channelId, task, algCode);
            return nullptr;
        }

        auto inst = std::make_shared<InstT>(action);
        if (!inst->AddTask(channelId, task)) {
            LOG_WARN("[Channel:{} Task:{}] Get {} Inst Failed", channelId, task, algCode);
            return nullptr;
        }
        if (!action.configObject.params.empty())
            inst->ModifyParam(channelId, task, action.configObject.params);
        algInsts.push_back(inst);
        LOG_INFO("[{}] Create Channel:{} Task:{}. Active shared instances for {}: {}", mng_name, channelId,
                 task, algCode, algInsts.size());
        return inst;
    }

    bool DeleteInst(InstPtr inst, const std::string& channelId, const std::string& task) {
        if (!inst) {
            LOG_WARN("[{}] Delete Channel:{} Task:{} But Inst Is Empty", mng_name, channelId, task);
            return false;
        }

        std::lock_guard<std::shared_mutex> lock(m_mtx);
        if (!inst->RemoveTask(channelId, task)) {
            LOG_WARN("[{}] Delete Channel:{} Task:{} But RemoveTask Failed", mng_name, channelId, task);
            return false;
        }

        if (inst->TaskIsEmpty()) {
            auto algCode   = inst->GetAlgCode();
            auto& algInsts = m_insts[algCode];
            auto it        = std::find(algInsts.begin(), algInsts.end(), inst);
            if (it != algInsts.end()) {
                algInsts.erase(it);
                LOG_INFO("[{}] Delete Channel:{} Task:{} Delete Inst. Active shared instances for {}: {}",
                         mng_name, channelId, task, algCode, algInsts.size());
            } else {
                LOG_WARN(
                    "[{}] Delete Channel:{} Task:{} Delete Inst Failed (Inst not found in tracking list)",
                    mng_name, channelId, task);
            }
            if (algInsts.empty()) {
                m_insts.erase(algCode);
            }
        } else {
            LOG_INFO("[{}] Delete Channel:{} Task:{} But Inst Already Have Tasks", mng_name, channelId, task);
        }

        return true;
    }

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus,
                     unsigned int durationSec = 30) override {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        for (auto it = m_insts.begin(); it != m_insts.end(); it++) {
            for (auto& inst : it->second) {
                inst->QueueStatus(queStatus, durationSec);
            }
        }
    }

    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) override {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        for (auto it = m_insts.begin(); it != m_insts.end(); it++) {
            for (auto& inst : it->second) {
                inst->ActionInfo(actionInfos);
            }
        }
    }

protected:
    std::shared_mutex m_mtx;
    std::map<std::string, std::vector<InstPtr>> m_insts;
    std::string mng_name;
};

}  // namespace cosmo
