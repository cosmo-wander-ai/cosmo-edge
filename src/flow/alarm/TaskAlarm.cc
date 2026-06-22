// TaskAlarm — Alarm terminal node: pushes alarms directly.

#include "flow/alarm/TaskAlarm.h"

#include <sstream>
#include <string_view>

#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/common/LlmYesNoJudge.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmRecordService.h"
#include "service/event/IEventNotifier.h"
#include "service/path/IFileService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/NToL.h"
#include "util/PathUtil.h"
#include "util/Rect.h"
#include "util/SafeParse.h"
#include "util/TimeUtil.h"
#include "util/dto/ClientMsgEvent.h"

namespace chrono = std::chrono;

// #define DURATION_LOG

static constexpr const char* kTag = "TaskAlarm ";
namespace cosmo {
namespace {

    bool SetLlmOpenAiConfigValue(TaskAlarmParam& param, std::string_view key, const std::string& value) {
        if (key == key::llm::PROVIDER) {
            param.llmOpenAiConfig.provider = value;
            return true;
        }
        if (key == key::llm::OPENAI_BASE_URL) {
            param.llmOpenAiConfig.base_url = value;
            return true;
        }
        if (key == key::llm::OPENAI_API_KEY) {
            param.llmOpenAiConfig.api_key = value;
            return true;
        }
        if (key == key::llm::OPENAI_MODEL) {
            param.llmOpenAiConfig.model = value;
            return true;
        }
        if (key == key::llm::OPENAI_ENDPOINT) {
            param.llmOpenAiConfig.endpoint = value;
            return true;
        }
        if (key == key::llm::OPENAI_TIMEOUT_MS) {
            param.llmOpenAiConfig.timeout_ms = util::ParseInt(value, param.llmOpenAiConfig.timeout_ms);
            return true;
        }
        if (key == key::llm::OPENAI_MAX_TOKENS) {
            param.llmOpenAiConfig.max_tokens = util::ParseInt(value, param.llmOpenAiConfig.max_tokens);
            return true;
        }
        return false;
    }

}  // namespace

TaskAlarm::~TaskAlarm() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

TaskAlarm::TaskAlarm(const std::string& channelId, const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBATaskAlarm, action, channelId, taskId),
      TaskAlarmSuppression(taskId),
      m_lastAlarmTime(chrono::steady_clock::now()),
      m_overviewRecInst(taskId, "alarm") {
    action_status = util::ErrorEnum::ActionReady;
    data_queue->SetMaxSize(3);

    for (auto& el : action.configObject.params) {
        if (key::alarm::PROPERTY == el.key.ToString()) {
            int value      = util::ParseInt(el.value);
            m_propertyType = static_cast<OnEventsPropertyType>(value);
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, el.value);

        } else if (key::face::SCALE_SIDE == el.key.ToString()) {
            auto value = util::ParseFloat(el.value);
            if (value >= 1.0f) {
                m_param.faceScaleParam = value;
                LOG_INFO("[{}/{}] Set {} To {}", GetTaskId(), GetFlowActionId(), el.key, el.value);
            }
        } else if (key::alarm::RT_EVENT_RECORD == el.key.ToString()) {
            auto value                      = util::ParseInt(el.value);
            m_param.realtimeEventRecordType = value ? true : false;
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, el.value);

        } else if (key::alarm::TR_EVENT_RECORD == el.key.ToString()) {
            auto value                     = util::ParseInt(el.value);
            m_param.triggerEventRecordType = value ? true : false;
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, el.value);
        } else if (key::llm::REVIEW == el.key.ToString()) {
            auto value              = util::ParseInt(el.value);
            m_param.enableLlmReview = value ? true : false;
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, el.value);
        } else if (key::llm::ATOMIC_CODE == el.key.ToString()) {
            m_param.llmAtomicCode = el.value;
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, el.value);
        } else if (key::llm::REVIEW_CONTENT == el.key.ToString()) {
            m_param.llmReviewContent = el.value;
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, el.value);
        } else if (SetLlmOpenAiConfigValue(m_param, el.key.ToString(), el.value.ToString())) {
            std::string logValue =
                el.key.ToString() == key::llm::OPENAI_API_KEY ? "***" : el.value.ToString();
            LOG_INFO("{}Task:{} Init {} Set To {}", kTag, task_id, el.key, logValue);
        }
    }
    LOG_INFO("{}Task:{} Init", kTag, task_id);
}

void TaskAlarm::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) {
    AlgActionDataQueueStatus status;
    if (data_queue->Status(status.queueStatus, durationSec)) {
        status.actionId = GetActionId();
        status.taskIds.push_back(task_id);
        status.actionStatus = action_status;
        status.alarmCount   = m_alarmCount;
        queStatus.push_back(status);
    }
    return;
}

void TaskAlarm::ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) {
    ActionRuntimeInfo actionInfoEl;
    actionInfoEl.actionId = GetActionId();
    // Alarm is the terminal node — no child info
    actionInfos.push_back(actionInfoEl);
}

/*
param.alarmInterval
*/
bool TaskAlarm::AnalysisKey(MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            task_id);
        return false;
    }
    if (param.keys.size() != 2) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. key size:{}",
            task_id, param.key, param.keys.size());
        return false;
    }

    if (key::PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] param.keys[0] is Not {}",
            task_id, key::PARAM);
        return false;
    }

    if (param.keys[1] == key::alarm::INTERVAL) {
        auto value = util::ParseInt(param.value);
        if (value != m_param.alarmInterval) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set alarmInterval From {} To {}",
                task_id, m_param.alarmInterval, value);
            m_param.alarmInterval = value;
        }
    } else if (param.keys[1] == key::alarm::TARGET_INTERVAL) {
        auto value = util::ParseInt(param.value);
        if (value != m_param.targetAlarmInterval) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set targetAlarmInterval From {} To {}",
                task_id, m_param.targetAlarmInterval, value);
            m_param.targetAlarmInterval = value;
        }
    } else if (param.keys[1] == key::alarm::TARGET_COUNT) {
        auto value = util::ParseInt(param.value);
        if (value != m_param.targetAlarmCount) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set targetAlarmCount From {} To {}",
                task_id, m_param.targetAlarmCount, value);
            m_param.targetAlarmCount = value;
        }
    } else if (param.keys[1] == key::alarm::RESTRAIN_SWITCH) {
        auto value = util::ParseInt(param.value);
        if (value != m_param.restrainSwitch) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set restrainSwitch From {} To {}",
                task_id, m_param.restrainSwitch, value);
            m_param.restrainSwitch = value;
        }
    } else if (param.keys[1] == key::alarm::OVERLAP_RATE) {
        auto value = util::ParseFloat(param.value);
        if (value != m_param.overlapRate) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set overlapRate From {} To {}",
                task_id, m_param.overlapRate, value);
            m_param.overlapRate = value;
        }
    } else if (param.keys[1] == key::alarm::RESTRAIN_TIME) {
        auto value = util::ParseInt(param.value);
        if (value != m_param.restrainTime) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set restrainTime From {} To {}",
                task_id, m_param.restrainTime, value);
            m_param.restrainTime = value;
        }
    } else if (param.keys[1] == key::alarm::OVERLAY_TRAJECTORY) {
        auto value = util::ParseInt(param.value);
        if (value != m_param.overlayTrajectory) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set overlayTrajectory From {} To {}",
                task_id, m_param.overlayTrajectory, value);
            m_param.overlayTrajectory = value ? true : false;
        }
    } else if (param.keys[1] == key::alarm::TASK_ID) {
        LOG_INFO(
            "ModifyParam "
            "[{}] {} Set To {}",
            task_id, param.keys[1], param.value);
        m_param.taskId = param.value;
    } else if (param.keys[1] == key::alarm::VIDEO_CHANNEL_ID) {
        LOG_INFO(
            "ModifyParam "
            "[{}] {} Set To {}",
            task_id, param.keys[1], param.value);
        m_param.videoChannelId = param.value;
    } else if (param.keys[1] == key::llm::REVIEW) {
        auto value = util::ParseInt(param.value);
        if (value != static_cast<int>(m_param.enableLlmReview)) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set enableLlmReview From {} To {}",
                task_id, m_param.enableLlmReview, value);
            m_param.enableLlmReview = value ? true : false;
        }
    } else if (param.keys[1] == key::llm::ATOMIC_CODE) {
        if (param.value != m_param.llmAtomicCode) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set llmAtomicCode From {} To {}",
                task_id, m_param.llmAtomicCode, param.value);
            m_param.llmAtomicCode = param.value;
        }
    } else if (param.keys[1] == key::llm::REVIEW_CONTENT) {
        if (param.value != m_param.llmReviewContent) {
            LOG_INFO(
                "ModifyParam "
                "[{}] Set llmReviewContent From {} To {}",
                task_id, m_param.llmReviewContent, param.value);
            m_param.llmReviewContent = param.value;
        }
    } else if (SetLlmOpenAiConfigValue(m_param, param.keys[1], param.value.ToString())) {
        std::string logValue = param.keys[1] == key::llm::OPENAI_API_KEY ? "***" : param.value.ToString();
        LOG_INFO(
            "ModifyParam "
            "[{}] Set {} To {}",
            task_id, param.keys[1], logValue);
    } else {
        return false;
    }

    return true;
}

// Modify parameters — incremental update on existing params
bool TaskAlarm::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        AnalysisKey(param);
    }

    return false;
}

// Set parameters — clear previous params and apply full replacement
bool TaskAlarm::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                         std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear existing params first
    m_param = {};
    for (auto& param : params) {
        AnalysisKey(param);
    }

    return false;
}

// Alarm handling — moved to TaskAlarmHandler.cc

}  // namespace cosmo
