// DinoDetector.cc — Grounding DINO detector: init, task management, and lifecycle.
// Parameter parsing and inference logic is in DinoDetectorParam.cc.

#include "flow/detect/DinoDetector.h"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <thread>

#include "flow/common/AlgDataRecord.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/system/IHardwareQuery.h"
#include "util/GeometricPos.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "DINO-DETECTER ";

namespace cosmo {
namespace {

    bool ParseDinoConfidence(const std::string& text, float& value) {
        try {
            std::size_t consumed = 0;
            const float parsed   = std::stof(text, &consumed);
            if (consumed != text.size() || !std::isfinite(parsed) || parsed < 0.0f || parsed > 1.0f)
                return false;
            value = parsed;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool IsDinoPromptParam(const MsgDynamicKeyValue& param) {
        const std::string key = param.key.ToString();
        return key == "keywords" || key == "prompt" || (!param.keys.empty() && param.keys[0] == "keywords") ||
               (param.keys.size() >= 2 && param.keys[1] == "prompt");
    }

    bool NormalizeDinoPrompt(const std::string& text, std::string& prompt) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return false;
        const auto last = text.find_last_not_of(" \t\r\n");
        prompt          = text.substr(first, last - first + 1);
        return !prompt.empty();
    }

}  // namespace

DinoDetector::~DinoDetector() {
    is_detector_inst_init_ = false;
    LOG_INFO("{}[{} {}] Stop", kTag, alg_code_, uuid);
    Stop();
    if (detector_) {
        if (running) {
            running = false;
            stop();
        }

        while (data_queue->RestSize() > 0) {
            data_queue->Pop();
        }

        detector_.reset();
        detector_ = nullptr;

        // Clear remaining resources
        channel_list_.clear();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, alg_code_, uuid);
}

DinoDetector::DinoDetector(ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionDinoDetect, action, "", "",
                    (action.atomicCode.empty() ? action.atomAlgName : action.atomicCode) + " DinoDetector") {
    action_status = util::ErrorEnum::ActionReady;
    // Prefer atomicCode; fall back to atomAlgName (consistent with ModelPathUtil lookup)
    alg_code_ = action.atomicCode.empty() ? action.atomAlgName : action.atomicCode;
    uuid      = util::GenerateUUID();

    batch_count_     = 1;
    max_reuse_count_ = 6;
    data_queue->SetMaxSize(144);

    LOG_INFO("{}[{} {}] Init MaxReuse:{} BatchCount:{}", kTag, alg_code_, uuid, max_reuse_count_,
             batch_count_);
}

bool DinoDetector::DinoSdkInit() {
    if (detector_) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, alg_code_, uuid);
        return true;
    }

    // Retry throttle: after 3 consecutive failures, wait 30s between retries
    constexpr int kMaxRetryBeforeDelay = 3;
    constexpr int64_t kRetryIntervalMs = 30 * 1000;  // 30 seconds
    // constexpr int64_t kRequiredGpuMemMB = 1024;       // Require at least 1GB available VRAM

    if (init_retry_count_ >= kMaxRetryBeforeDelay) {
        auto now = util::GetMilliseconds();
        if ((now - last_init_fail_time_ms_) < kRetryIntervalMs) {
            return false;  // Silently skip to avoid log flooding
        }
        LOG_INFO("{}[{} {}] Init retry after {}s cooldown (retryCount:{})", kTag, alg_code_, uuid,
                 kRetryIntervalMs / 1000, init_retry_count_);
    }

    // GPU VRAM pre-check (currently disabled)
    // auto availMB =
    // service::ServiceRegistry::Instance().Get<service::IHardwareQuery>().GetAvailableGpuMemoryMB();
    // if (availMB >= 0 && availMB < kRequiredGpuMemMB) {
    //     init_retry_count_++;
    //     last_init_fail_time_ms_ = util::GetMilliseconds();
    // }

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet = service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
        alg_code_, cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", kTag, alg_code_, cfgRet);
        return false;
    }

    // Get vocab path (same directory as model)
    std::string modelDir  = modelPath.substr(0, modelPath.find_last_of("/\\"));
    std::string vocabPath = modelDir + "/vocab.txt";

    LOG_INFO("{}cfgPath:{}, modelPath:{}, vocabPath:{}", kTag, cfgPath, modelPath, vocabPath);

    detector_ = std::make_shared<DinoDetectorUnify>(alg_code_, cfgPath, modelPath, vocabPath);
    auto ret  = detector_->Init();
    if (util::ErrorEnum::Success != ret) {
        detector_.reset();
        detector_     = nullptr;
        action_status = ret;
        init_retry_count_++;
        last_init_fail_time_ms_ = util::GetMilliseconds();
        LOG_WARN("{}[{} {}] {} Sdk Init Failed Ret:{} (retryCount:{})", kTag, alg_code_, uuid, alg_code_, ret,
                 init_retry_count_);
        return false;
    }

    init_retry_count_ = 0;  // Reset on success
    action_status     = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("{}[{} {}] {} Init Sdk", kTag, alg_code_, uuid, alg_code_);
    is_detector_inst_init_ = true;
    return true;
}

bool DinoDetector::ValidKey(const MsgDynamicKeyValue& param) const {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            alg_code_, uuid);
        return false;
    }
    return true;
}

bool DinoDetector::AnalysisKey(const std::string& /*channel_id*/, const std::string& taskId,
                               const MsgDynamicKeyValue& param) {
    // Parse detection prompt parameter ('keywords', 'prompt', or 'param.prompt')
    std::string keyStr = param.key.ToString();
    bool isPrompt      = (keyStr == "keywords") || (keyStr == "prompt") ||
                    (param.keys.size() >= 1 && param.keys[0] == "keywords") ||
                    (param.keys.size() >= 2 && param.keys[1] == "prompt");
    if (isPrompt) {
        std::string value;
        if (!NormalizeDinoPrompt(param.value.ToString(), value)) {
            LOG_WARN("ModifyParam [{} {}] Task:{} DINO prompt is empty", alg_code_, uuid, taskId);
            return false;
        }
        auto it = std::find_if(params_.param.begin(), params_.param.end(),
                               [&taskId](const DinoDetectorParamEl& el) { return el.task_id == taskId; });
        if (it != params_.param.end()) {
            it->prompt = value;
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} set prompt: \"{}\"",
                alg_code_, uuid, taskId, value);
        } else {
            DinoDetectorParamEl el;
            el.task_id = taskId;
            el.prompt  = value;
            params_.param.push_back(el);
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} add param prompt: \"{}\"",
                alg_code_, uuid, taskId, value);
        }
        return true;
    }

    // Parse confidence parameters: aiParam.box.confidence / aiParam.text.confidence
    if ((param.keys.size() == 3) && (key::AI_PARAM == param.keys[0]) && (key::CONFIDENCE == param.keys[2])) {
        float confValue = 0.0f;
        if (!ParseDinoConfidence(param.value.ToString(), confValue)) {
            LOG_WARN("ModifyParam [{} {}] Task:{} invalid {} confidence: {}", alg_code_, uuid, taskId,
                     param.keys[1], param.value.ToString());
            return false;
        }
        std::string subKey = param.keys[1];  // "box" or "text"
        auto it            = std::find_if(params_.param.begin(), params_.param.end(),
                                          [&taskId](const DinoDetectorParamEl& el) { return el.task_id == taskId; });
        if (it == params_.param.end()) {
            DinoDetectorParamEl el;
            el.task_id = taskId;
            params_.param.push_back(el);
            it = std::prev(params_.param.end());
        }
        if (subKey == "box") {
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} set box_confidence: {} -> {}",
                alg_code_, uuid, taskId, it->box_confidence, confValue);
            it->box_confidence = confValue;
        } else if (subKey == "text") {
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} set text_confidence: {} -> {}",
                alg_code_, uuid, taskId, it->text_confidence, confValue);
            it->text_confidence = confValue;
        } else {
            LOG_WARN("ModifyParam [{} {}] Task:{} unknown aiParam subKey: {}", alg_code_, uuid, taskId,
                     subKey);
            return false;
        }
        return true;
    }

    return true;
}

bool DinoDetector::ModifyParam(const std::string& channel_id, const std::string& taskId,
                               std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    const auto original = params_;
    if (!ApplyParamsUnlocked(channel_id, taskId, params)) {
        params_ = original;
        return false;
    }
    params_.param_modify_sign++;
    return true;
}

bool DinoDetector::ApplyParamsUnlocked(const std::string& channel_id, const std::string& taskId,
                                       std::vector<MsgDynamicKeyValue>& params) {
    bool valid = true;
    for (auto& param : params) {
        // Parse keywords/prompt even if keys is empty (orchestration may only have key/value)
        if (IsDinoPromptParam(param)) {
            valid = AnalysisKey(channel_id, taskId, param) && valid;
            continue;
        }
        // Also parse aiParam.box.confidence / aiParam.text.confidence parameters
        bool isAiParam =
            (param.keys.size() == 3) && (!param.keys.empty()) && (key::AI_PARAM == param.keys[0]);
        if (isAiParam) {
            valid = AnalysisKey(channel_id, taskId, param) && valid;
            continue;
        }
        if (!ValidKey(param)) {
            continue;
        }
        valid = AnalysisKey(channel_id, taskId, param) && valid;
    }
    return valid;
}

bool DinoDetector::SetParam(const std::string& channel_id, const std::string& taskId,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    const auto original = params_;
    std::string savedPrompt;
    auto it = std::find_if(params_.param.begin(), params_.param.end(),
                           [&taskId](const auto& p) { return p.task_id == taskId; });
    if (it != params_.param.end()) {
        savedPrompt = it->prompt;
        params_.param.erase(it);
    }

    if (!ApplyParamsUnlocked(channel_id, taskId, params)) {
        params_ = original;
        return false;
    }
    const bool replaces_prompt =
        std::any_of(params.begin(), params.end(), [](const auto& param) { return IsDinoPromptParam(param); });
    if (!replaces_prompt && !savedPrompt.empty()) {
        it = std::find_if(params_.param.begin(), params_.param.end(),
                          [&taskId](const DinoDetectorParamEl& el) { return el.task_id == taskId; });
        if (it == params_.param.end()) {
            DinoDetectorParamEl el;
            el.task_id = taskId;
            el.prompt  = savedPrompt;
            params_.param.push_back(el);
        } else {
            it->prompt = savedPrompt;
        }
    }
    params_.param_modify_sign++;
    return true;
}

bool DinoDetector::SetArea(const std::string& /*channel_id*/, const std::string& taskId,
                           std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shielded_areas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    auto& taskArea         = task_areas_[taskId];
    taskArea.taskId        = taskId;
    taskArea.areas         = areas;
    taskArea.shieldedAreas = shielded_areas;
    return true;
}

void DinoDetector::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status, unsigned int duration_sec) {
    AlgActionDataQueueStatus status;
    auto durationInfo = duration_stat.ComputeStats();
    status.durationInfos.push_back(durationInfo);
    if (data_queue->Status(status.queueStatus, duration_sec)) {
        status.actionId = GetActionId();
        for (auto& channelNode : channel_list_) {
            status.channelIds.push_back(channelNode.channel);
            status.taskIds.insert(status.taskIds.end(), channelNode.tasks.begin(), channelNode.tasks.end());
        }
        status.actionStatus = action_status;
        que_status.push_back(status);
    }
}

bool DinoDetector::AddTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    auto chIt = std::find_if(channel_list_.begin(), channel_list_.end(),
                             [&channel_id](const auto& ch) { return ch.channel == channel_id; });
    if (chIt != channel_list_.end()) {
        auto tIt = std::find(chIt->tasks.begin(), chIt->tasks.end(), task);
        if (tIt != chIt->tasks.end()) {
            LOG_WARN("{}[{} {}] Channel:{} Task:{} Already Exist", kTag, alg_code_, uuid, channel_id, task);
            return true;
        }
        chIt->tasks.push_back(task);
        AddOverviewTask(task);
        LOG_INFO("{}[{} {}] Add Task Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id, task);
        return true;
    }

    DinoDetectorChannel newChannel;
    newChannel.channel = channel_id;
    newChannel.tasks.push_back(task);
    channel_list_.push_back(newChannel);
    AddOverviewTask(task);
    LOG_INFO("{}[{} {}] Add Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id, task);
    return true;
}

bool DinoDetector::RemoveTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    auto chIt = std::find_if(channel_list_.begin(), channel_list_.end(),
                             [&channel_id](const auto& ch) { return ch.channel == channel_id; });
    if (chIt != channel_list_.end()) {
        auto taskIt = std::find(chIt->tasks.begin(), chIt->tasks.end(), task);
        if (taskIt != chIt->tasks.end()) {
            chIt->tasks.erase(taskIt);
            LOG_INFO("{}[{} {}] Remove Task Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id, task);

            if (chIt->tasks.empty()) {
                channel_list_.erase(chIt);
                LOG_INFO("{}[{} {}] Remove Channel:{}", kTag, alg_code_, uuid, channel_id);
            }
            // Clean up associated data to prevent map growth / memory leak on repeated add/remove
            task_areas_.erase(task);
            overview_rec_insts_.erase(task);
            params_.param.erase(
                std::remove_if(params_.param.begin(), params_.param.end(),
                               [&task](const DinoDetectorParamEl& param) { return param.task_id == task; }),
                params_.param.end());
            return true;
        }
    }

    LOG_WARN("{}[{} {}] Remove Task Channel:{} Task:{} Not Found", kTag, alg_code_, uuid, channel_id, task);
    return false;
}

bool DinoDetector::ChannelExist(const std::string& channel_id) const {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return std::any_of(channel_list_.begin(), channel_list_.end(),
                       [&channel_id](const auto& ch) { return ch.channel == channel_id; });
}

// Parameter and task management — moved to DinoDetectorParam.cc

}  // namespace cosmo
