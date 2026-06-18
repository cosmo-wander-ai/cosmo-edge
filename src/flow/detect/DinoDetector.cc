// DinoDetector.cc — Grounding DINO detector: init, task management, and lifecycle.
// Parameter parsing and inference logic is in DinoDetectorParam.cc.

#include "flow/detect/DinoDetector.h"

#include <unistd.h>

#include <algorithm>
#include <map>
#include <thread>

#include "flow/common/AlgDataRecord.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
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
DinoDetector::~DinoDetector() {
    m_detectorInstInit = false;
    LOG_INFO("{}[{} {}] Stop", kTag, m_algCode, uuid);
    Stop();
    if (m_detector) {
        if (running) {
            running = false;
            stop();
        }

        while (data_queue->RestSize() > 0) {
            data_queue->Pop();
        }

        m_detector.reset();
        m_detector = nullptr;

        // Clear remaining resources
        m_channelList.clear();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, m_algCode, uuid);
}

DinoDetector::DinoDetector(ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionDinoDetect, action, "", "",
                    (action.atomicCode.empty() ? action.atomAlgName : action.atomicCode) + " DinoDetector") {
    action_status = util::ErrorEnum::ActionReady;
    // Prefer atomicCode; fall back to atomAlgName (consistent with ModelPathUtil lookup)
    m_algCode = action.atomicCode.empty() ? action.atomAlgName : action.atomicCode;
    uuid      = util::GenerateUUID();

    m_batchCount    = 1;
    m_maxReuseCount = 6;
    data_queue->SetMaxSize(144);

    LOG_INFO("{}[{} {}] Init MaxReuse:{} BatchCount:{}", kTag, m_algCode, uuid, m_maxReuseCount,
             m_batchCount);
}

bool DinoDetector::DinoSdkInit() {
    if (m_detector) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, m_algCode, uuid);
        return true;
    }

    // Retry throttle: after 3 consecutive failures, wait 30s between retries
    constexpr int kMaxRetryBeforeDelay = 3;
    constexpr int64_t kRetryIntervalMs = 30 * 1000;  // 30 seconds
    // constexpr int64_t kRequiredGpuMemMB = 1024;       // Require at least 1GB available VRAM

    if (m_initRetryCount >= kMaxRetryBeforeDelay) {
        auto now = util::GetMilliseconds();
        if ((now - m_lastInitFailTimeMs) < kRetryIntervalMs) {
            return false;  // Silently skip to avoid log flooding
        }
        LOG_INFO("{}[{} {}] Init retry after {}s cooldown (retryCount:{})", kTag, m_algCode, uuid,
                 kRetryIntervalMs / 1000, m_initRetryCount);
    }

    // GPU VRAM pre-check (currently disabled)
    // auto availMB =
    // service::ServiceRegistry::Instance().Get<service::IHardwareQuery>().GetAvailableGpuMemoryMB();
    // if (availMB >= 0 && availMB < kRequiredGpuMemMB) {
    //     m_initRetryCount++;
    //     m_lastInitFailTimeMs = util::GetMilliseconds();
    // }

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet           = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
                  m_algCode, cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", kTag, m_algCode, cfgRet);
        return false;
    }

    // Get vocab path (same directory as model)
    std::string modelDir  = modelPath.substr(0, modelPath.find_last_of("/\\"));
    std::string vocabPath = modelDir + "/vocab.txt";

    LOG_INFO("{}cfgPath:{}, modelPath:{}, vocabPath:{}", kTag, cfgPath, modelPath, vocabPath);

    m_detector = std::make_shared<DinoDetectorUnify>(m_algCode, cfgPath, modelPath, vocabPath);
    auto ret   = m_detector->Init();
    if (util::ErrorEnum::Success != ret) {
        m_detector.reset();
        m_detector    = nullptr;
        action_status = ret;
        m_initRetryCount++;
        m_lastInitFailTimeMs = util::GetMilliseconds();
        LOG_WARN("{}[{} {}] {} Sdk Init Failed Ret:{} (retryCount:{})", kTag, m_algCode, uuid, m_algCode, ret,
                 m_initRetryCount);
        return false;
    }

    m_initRetryCount = 0;  // Reset on success
    action_status    = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("{}[{} {}] {} Init Sdk", kTag, m_algCode, uuid, m_algCode);
    m_detectorInstInit = true;
    return true;
}

bool DinoDetector::ValidKey(MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            m_algCode, uuid);
        return false;
    }
    return true;
}

bool DinoDetector::AnalysisKey(const std::string& /*channelId*/, const std::string& taskId,
                               MsgDynamicKeyValue& param) {
    // Parse detection prompt parameter ('keywords', 'prompt', or 'param.prompt')
    std::string keyStr = param.key.ToString();
    bool isPrompt      = (keyStr == "keywords") || (keyStr == "prompt") ||
                    (param.keys.size() >= 1 && param.keys[0] == "keywords") ||
                    (param.keys.size() >= 2 && param.keys[1] == "prompt");
    if (isPrompt) {
        std::string value = param.value.ToString();
        auto it           = std::find_if(m_params.param.begin(), m_params.param.end(),
                                         [&taskId](const DinoDetectorParamEl& el) { return el.taskId == taskId; });
        if (it != m_params.param.end()) {
            it->prompt = value;
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} set prompt: \"{}\"",
                m_algCode, uuid, taskId, value);
        } else {
            DinoDetectorParamEl el;
            el.taskId = taskId;
            el.prompt = value;
            m_params.param.push_back(el);
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} add param prompt: \"{}\"",
                m_algCode, uuid, taskId, value);
        }
        return true;
    }

    // Parse confidence parameters: aiParam.box.confidence / aiParam.text.confidence
    if ((param.keys.size() == 3) && (key::AI_PARAM == param.keys[0]) && (key::CONFIDENCE == param.keys[2])) {
        float confValue    = util::ParseFloat(param.value.ToString());
        std::string subKey = param.keys[1];  // "box" or "text"
        auto it            = std::find_if(m_params.param.begin(), m_params.param.end(),
                                          [&taskId](const DinoDetectorParamEl& el) { return el.taskId == taskId; });
        if (it == m_params.param.end()) {
            DinoDetectorParamEl el;
            el.taskId = taskId;
            m_params.param.push_back(el);
            it = std::prev(m_params.param.end());
        }
        if (subKey == "box") {
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} set boxConfidence: {} -> {}",
                m_algCode, uuid, taskId, it->boxConfidence, confValue);
            it->boxConfidence = confValue;
        } else if (subKey == "text") {
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} set textConfidence: {} -> {}",
                m_algCode, uuid, taskId, it->textConfidence, confValue);
            it->textConfidence = confValue;
        } else {
            LOG_DEBUG(
                "ModifyParam "
                "[{} {}] Task:{} unknown aiParam subKey: {}",
                m_algCode, uuid, taskId, subKey);
        }
        return true;
    }

    return true;
}

bool DinoDetector::ModifyParam(const std::string& channelId, const std::string& taskId,
                               std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        // Parse keywords/prompt even if keys is empty (orchestration may only have key/value)
        std::string keyStr = param.key.ToString();
        bool isPromptKey   = (keyStr == "keywords") || (keyStr == "prompt") ||
                           (param.keys.size() >= 1 && param.keys[0] == "keywords") ||
                           (param.keys.size() >= 2 && param.keys[1] == "prompt");
        if (isPromptKey) {
            AnalysisKey(channelId, taskId, param);
            continue;
        }
        // Also parse aiParam.box.confidence / aiParam.text.confidence parameters
        bool isAiParam =
            (param.keys.size() == 3) && (!param.keys.empty()) && (key::AI_PARAM == param.keys[0]);
        if (isAiParam) {
            AnalysisKey(channelId, taskId, param);
            continue;
        }
        if (!ValidKey(param)) {
            continue;
        }
        AnalysisKey(channelId, taskId, param);
    }
    m_params.paramModifySign++;
    return true;
}

bool DinoDetector::SetParam(const std::string& channelId, const std::string& taskId,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::string savedPrompt;
    {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = std::find_if(m_params.param.begin(), m_params.param.end(),
                               [&taskId](const auto& p) { return p.taskId == taskId; });
        if (it != m_params.param.end()) {
            savedPrompt = it->prompt;
        }
        m_params.param.clear();
    }
    bool ret = ModifyParam(channelId, taskId, params);
    if (!savedPrompt.empty()) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = std::find_if(m_params.param.begin(), m_params.param.end(),
                               [&taskId](const DinoDetectorParamEl& el) { return el.taskId == taskId; });
        if (it == m_params.param.end()) {
            DinoDetectorParamEl el;
            el.taskId = taskId;
            el.prompt = savedPrompt;
            m_params.param.push_back(el);
        }
    }
    return ret;
}

bool DinoDetector::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                           std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    auto& taskArea         = m_taskAreas[taskId];
    taskArea.taskId        = taskId;
    taskArea.areas         = areas;
    taskArea.shieldedAreas = shieldedAreas;
    return true;
}

void DinoDetector::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) {
    AlgActionDataQueueStatus status;
    auto durationInfo = duration_stat.ComputeStats();
    status.durationInfos.push_back(durationInfo);
    if (data_queue->Status(status.queueStatus, durationSec)) {
        status.actionId = GetActionId();
        for (auto& channelNode : m_channelList) {
            status.channelIds.push_back(channelNode.channel);
            status.taskIds.insert(status.taskIds.end(), channelNode.tasks.begin(), channelNode.tasks.end());
        }
        status.actionStatus = action_status;
        queStatus.push_back(status);
    }
}

bool DinoDetector::AddTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    auto chIt = std::find_if(m_channelList.begin(), m_channelList.end(),
                             [&channel_id](const auto& ch) { return ch.channel == channel_id; });
    if (chIt != m_channelList.end()) {
        auto tIt = std::find(chIt->tasks.begin(), chIt->tasks.end(), task);
        if (tIt != chIt->tasks.end()) {
            LOG_WARN("{}[{} {}] Channel:{} Task:{} Already Exist", kTag, m_algCode, uuid, channel_id, task);
            return true;
        }
        chIt->tasks.push_back(task);
        AddOverviewTask(task);
        LOG_INFO("{}[{} {}] Add Task Channel:{} Task:{}", kTag, m_algCode, uuid, channel_id, task);
        return true;
    }

    DinoDetectorChannel newChannel;
    newChannel.channel = channel_id;
    newChannel.tasks.push_back(task);
    m_channelList.push_back(newChannel);
    AddOverviewTask(task);
    LOG_INFO("{}[{} {}] Add Channel:{} Task:{}", kTag, m_algCode, uuid, channel_id, task);
    return true;
}

bool DinoDetector::RemoveTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    auto chIt = std::find_if(m_channelList.begin(), m_channelList.end(),
                             [&channel_id](const auto& ch) { return ch.channel == channel_id; });
    if (chIt != m_channelList.end()) {
        auto taskIt = std::find(chIt->tasks.begin(), chIt->tasks.end(), task);
        if (taskIt != chIt->tasks.end()) {
            chIt->tasks.erase(taskIt);
            LOG_INFO("{}[{} {}] Remove Task Channel:{} Task:{}", kTag, m_algCode, uuid, channel_id, task);

            if (chIt->tasks.empty()) {
                m_channelList.erase(chIt);
                LOG_INFO("{}[{} {}] Remove Channel:{}", kTag, m_algCode, uuid, channel_id);
            }
            // Clean up associated data to prevent map growth / memory leak on repeated add/remove
            m_taskAreas.erase(task);
            m_overviewRecInsts.erase(task);
            return true;
        }
    }

    LOG_WARN("{}[{} {}] Remove Task Channel:{} Task:{} Not Found", kTag, m_algCode, uuid, channel_id, task);
    return false;
}

bool DinoDetector::ChannelExist(const std::string& channel_id) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    return std::any_of(m_channelList.begin(), m_channelList.end(),
                       [&channel_id](const auto& ch) { return ch.channel == channel_id; });
}

// Parameter and task management — moved to DinoDetectorParam.cc

}  // namespace cosmo
