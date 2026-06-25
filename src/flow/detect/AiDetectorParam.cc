// AiDetectorParam.cc — Parameter management and confidence filtering for AiDetector.
// Split from AiDetector.cc to reduce file size (DEBT-007).

#include <algorithm>

#include "flow/detect/AiDetector.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "AI-DETECTER ";
namespace cosmo {

bool AiDetector::ValidKey(MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            m_name, uuid);
        return false;
    }
    if (param.keys.size() != 3) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] Set {} Failed. key size:{}",
            m_name, uuid, param.key, param.keys.size());
        return false;
    }

    if (key::AI_PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0]:{} is Not {}",
            m_name, uuid, param.keys[0], key::AI_PARAM);
        return false;
    }

    return true;
}

bool AiDetector::SetConfidenceToLocal(const std::string& /*channelId*/, const std::string& taskId,
                                      AiConfidence& confidence) {
    for (auto& param : m_params.param) {
        if (taskId == param.taskId) {
            auto it = std::find_if(param.labelParams.begin(), param.labelParams.end(),
                                   [&confidence](const auto& lp) { return confidence.label == lp.label; });
            if (it != param.labelParams.end()) {
                LOG_INFO(
                    "ModifyParam "
                    " Task:{} Detect Confidence {} Modify From {} To {}",
                    taskId, confidence.label, it->confidence, confidence.confidence);
                it->confidence = confidence.confidence;
                return true;
            }
            AiLabelParam labelParam;
            labelParam.label      = confidence.label;
            labelParam.confidence = confidence.confidence;
            param.labelParams.push_back(labelParam);
            LOG_INFO(
                "ModifyParam "
                " Task:{} Detect Confidence Add {} Value {}",
                taskId, confidence.label, confidence.confidence);
            return true;
        }
    }

    AiDetectorParamEl detParam;
    detParam.taskId = taskId;
    AiLabelParam labelParam;
    labelParam.label      = confidence.label;
    labelParam.confidence = confidence.confidence;
    detParam.labelParams.push_back(labelParam);
    m_params.param.push_back(detParam);
    LOG_INFO(
        "ModifyParam "
        " Add Task:{} Detect Confidence {} Value {}",
        taskId, confidence.label, confidence.confidence);

    return true;
}

bool AiDetector::SetTargetPosToLocal(const std::string& /*channelId*/, const std::string& taskId,
                                     const std::string& inLabel, TargetPosition pos) {
    for (auto& param : m_params.param) {
        if (taskId == param.taskId) {
            auto it = std::find_if(param.labelParams.begin(), param.labelParams.end(),
                                   [&inLabel](const auto& lp) { return inLabel == lp.label; });
            if (it != param.labelParams.end()) {
                LOG_INFO(
                    "ModifyParam "
                    " Task:{} Detect detPos {} Modify From {} To {}",
                    taskId, it->label, it->pos, pos);
                it->pos = pos;
                return true;
            }
            AiLabelParam labelParam;
            labelParam.label = inLabel;
            labelParam.pos   = pos;
            param.labelParams.push_back(labelParam);
            LOG_INFO(
                "ModifyParam "
                " Task:{} Detect detPos Add {} Value {} WARNING Need Confidence",
                taskId, labelParam.label, labelParam.pos);
            return true;
        }
    }

    AiDetectorParamEl detParam;
    detParam.taskId = taskId;
    AiLabelParam labelParam;
    labelParam.label = inLabel;
    labelParam.pos   = pos;
    detParam.labelParams.push_back(labelParam);
    m_params.param.push_back(detParam);
    LOG_INFO(
        "ModifyParam "
        " Add Task:{} Detect detPos {} Value {} WARNING Need Confidence",
        taskId, labelParam.label, labelParam.pos);

    return true;
}

bool AiDetector::AnalysisKey(const std::string& channelId, const std::string& taskId,
                             MsgDynamicKeyValue& param) {
    if (!ValidKey(param)) {
        return false;
    }

    if (param.keys[2] == key::CONFIDENCE) {
        AiConfidence confidence;
        confidence.label      = param.keys[1];
        confidence.confidence = util::ParseFloat(param.value);
        return SetConfidenceToLocal(channelId, taskId, confidence);
    }
    if (param.keys[2] == key::DET_POSITION) {
        TargetPosition value = static_cast<TargetPosition>(util::ParseInt(param.value));
        return SetTargetPosToLocal(channelId, taskId, param.keys[1], value);
    } else {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.key {} value {} is Unknown",
            m_name, uuid, param.key, param.value);
        return false;
    }
}

// Modify parameters — update on top of existing parameters
bool AiDetector::ModifyParam(const std::string& channelId, const std::string& taskId,
                             std::vector<MsgDynamicKeyValue>& params) {
    int modifyCount = 0;
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        auto ret = AnalysisKey(channelId, taskId, param);
        modifyCount += ret ? 1 : 0;
    }
    if (modifyCount) {
        m_params.paramModifySign += 1;
    }
    return false;
}

// Set parameters — clear previous parameters, set new ones
bool AiDetector::SetParam(const std::string& channelId, const std::string& taskId,
                          std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    m_params = {};
    for (auto& param : params) {
        AnalysisKey(channelId, taskId, param);
    }
    m_params.paramModifySign += 1;
    return true;
}

// Set areas — clear previous areas, set new ones
bool AiDetector::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                         std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    auto& taskArea         = m_taskAreas[taskId];
    taskArea.taskId        = taskId;
    taskArea.areas         = areas;
    taskArea.shieldedAreas = shieldedAreas;
    return true;
}

AiDetectorParamEl AiDetector::FoundLocalParamByTask(const AlgTaskUnit& task) {
    AiDetectorParamEl param;
    param.taskId      = task.task_id;
    float coefficient = 1.0f;
    if (AATrack_Code == task.actionId) {
        coefficient = 0.7f;
    }
    auto it = std::find_if(m_params.param.begin(), m_params.param.end(),
                           [&task](const auto& lp) { return task.task_id == lp.taskId; });
    if (it != m_params.param.end()) {
        for (auto& localParamConf : it->labelParams) {
            AiLabelParam labelParam;
            labelParam.label      = localParamConf.label;
            labelParam.confidence = localParamConf.confidence * coefficient;
            labelParam.pos        = localParamConf.pos;
            LOG_INFO("{}[{} {}] BindTask {} action:{} confidence:{}/{}. coefficient:{} detPostion:{}", kTag,
                     m_name, uuid, task.task_id, task.actionId, labelParam.label, labelParam.confidence,
                     coefficient, labelParam.pos);
            param.labelParams.push_back(labelParam);
        }
        return param;
    }

    return param;
}

struct AiTaskConfidenceEl {
    std::string taskId;
    AiConfidence confidence;
};

void AiDetector::FindActiveConfidence(const std::vector<AiDetectorParamEl>& shouldActiveParams) {
    std::vector<AiTaskConfidenceEl> taskConfidences;
    m_activeConfidence.clear();

    for (auto& lable : m_lables) {
        AiTaskConfidenceEl lableMinConfidence;
        lableMinConfidence.confidence.confidence = 100.0f;
        lableMinConfidence.confidence.label      = lable;

        for (auto& taskParam : shouldActiveParams) {
            for (auto& taskConfidence : taskParam.labelParams) {
                if (taskConfidence.label == lable) {
                    if (lableMinConfidence.confidence.confidence > taskConfidence.confidence) {
                        lableMinConfidence.confidence.confidence = taskConfidence.confidence;
                        lableMinConfidence.taskId                = taskParam.taskId;
                    }
                }
            }
        }

        if ((lableMinConfidence.confidence.confidence >= 0.0f) &&
            (lableMinConfidence.confidence.confidence <= 1.0f)) {
            AiConfidence confidence;
            confidence.confidence = lableMinConfidence.confidence.confidence;
            confidence.label      = lableMinConfidence.confidence.label;
            LOG_INFO("{}[{} {}] {} Confidece From Task {} Value Should Be {}.", kTag, m_name, uuid,
                     lableMinConfidence.confidence.label, lableMinConfidence.taskId,
                     lableMinConfidence.confidence.confidence);
            taskConfidences.push_back(lableMinConfidence);
            m_activeConfidence.push_back(confidence);
            m_activeConfidenceModifySign += 1;
        }
    }
}

void AiDetector::CheckAndActiveConfidece() {
    std::vector<AiDetectorParamEl> shouldActiveParams;
    if ((distributor->GetSign() != m_signRegister) ||
        (m_params.paramActiveSign != m_params.paramModifySign)) {
        m_activeTaskConfidence.clear();
        m_signRegister           = distributor->GetSign();
        m_params.paramModifySign = m_params.paramActiveSign;
        LOG_INFO("{}[{} {}] Confidece Should Be Active.", kTag, m_name, uuid);
        auto bindTasks = distributor->GetBindTasks();
        {
            std::shared_lock<std::shared_mutex> lock(mtx);
            for (auto& channelTask : bindTasks) {
                for (auto& task : channelTask.tasks) {
                    // Per-task confidence with tracking coefficient (0.7) for ConfidenceFilter.
                    // Tracking needs a lower threshold to keep candidates that might re-associate;
                    // this lowered value is correct for per-task filtering but must NOT leak to
                    // the detector's global SetThreshold.
                    auto taskConfidence = FoundLocalParamByTask(task);
                    LOG_INFO("{}[{} {}] BindTask {}/{}.", kTag, m_name, uuid, task.channel_id, task.task_id);
                    m_activeTaskConfidence.push_back(taskConfidence);

                    // Global detector threshold: use raw user-configured values without the
                    // tracking coefficient.  The 0.7× discount is a per-task tracking concern;
                    // applying it to SetThreshold would silently lower the model's effective
                    // detection threshold below what the user configured.
                    auto it = std::find_if(m_params.param.begin(), m_params.param.end(),
                                           [&task](const auto& lp) { return task.task_id == lp.taskId; });
                    if (it != m_params.param.end()) {
                        shouldActiveParams.push_back(*it);
                    }
                }
            }
        }
        FindActiveConfidence(shouldActiveParams);
    }
}

AiDetectorParamEl AiDetector::GetTaskLabelParams(const std::string& taskId) {
    auto it = std::find_if(m_activeTaskConfidence.begin(), m_activeTaskConfidence.end(),
                           [&taskId](const auto& el) { return el.taskId == taskId; });
    if (it != m_activeTaskConfidence.end()) {
        return *it;
    }

    return {};
}

TargetPosition AiDetector::GetTaskLabelPos(const std::string& label, const AiDetectorParamEl& taskLabels) {
    auto it = std::find_if(taskLabels.labelParams.begin(), taskLabels.labelParams.end(),
                           [&label](const auto& lp) { return lp.label == label; });
    if (it != taskLabels.labelParams.end()) {
        return it->pos;
    }

    return TargetPosition::kBottom;  // default
}

}  // namespace cosmo
