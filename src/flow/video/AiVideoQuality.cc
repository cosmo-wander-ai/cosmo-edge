// AI Video Quality Action.

#include "flow/video/AiVideoQuality.h"

#include <mutex>
#include <shared_mutex>

#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag = "AI-VIDEOQUALITY ";

namespace cosmo {

AiVideoQuality::~AiVideoQuality() {
    LOG_INFO("{}[{} {}] Stop", kTag, GetTaskId(), GetFlowActionId());
    Stop();
    LOG_INFO("{}[{} {}] Delete", kTag, GetTaskId(), GetFlowActionId());
}

AiVideoQuality::AiVideoQuality(const std::string& task, ActionNode& action_param)
    : AlgActionBase(AlgActionType::AlgActionAiVideoQuality, action_param, "", task) {
    for (const auto& action_param_iter : action_param.configObject.params) {
        if (key::diagnosis::TYPE == action_param_iter.key.ToString()) {
            auto value = util::ParseInt(action_param_iter.value);
            if (IsValidVideoQualityType(value)) {
                params_.type = static_cast<AiVideoQualityType>(value);
                LOG_INFO("{}[{} {}] Set {} To {} ", kTag, GetTaskId(), GetFlowActionId(),
                         action_param_iter.key, action_param_iter.value);
            } else {
                LOG_INFO("{}[{} {}] Set {} To {} Failed", kTag, GetTaskId(), GetFlowActionId(),
                         action_param_iter.key, action_param_iter.value);
            }
        } else if (key::diagnosis::THRESHOLD == action_param_iter.key.ToString()) {
            auto value = util::ParseFloat(action_param_iter.value);
            if ((value >= 0.0f) && (value <= 1.0f)) {
                params_.threshold = value;
                LOG_INFO("{}[{} {}] Set {} To {} ", kTag, GetTaskId(), GetFlowActionId(),
                         action_param_iter.key, action_param_iter.value);
            } else {
                LOG_INFO("{}[{} {}] Set {} To {} Failed", kTag, GetTaskId(), GetFlowActionId(),
                         action_param_iter.key, action_param_iter.value);
            }
        } else if (key::diagnosis::THRESHOLD_EXT == action_param_iter.key.ToString()) {
            auto value = util::ParseFloat(action_param_iter.value);
            if ((value >= 0.0f) && (value <= 1.0f)) {
                params_.threshold_ext = value;
                LOG_INFO("{}[{} {}] Set {} To {} ", kTag, GetTaskId(), GetFlowActionId(),
                         action_param_iter.key, action_param_iter.value);
            } else {
                LOG_INFO("{}[{} {}] Set {} To {} Failed", kTag, GetTaskId(), GetFlowActionId(),
                         action_param_iter.key, action_param_iter.value);
            }
        }
    }
    LOG_INFO("{}[{} {}] Init type:{} threshold:{} thresholdExt:{}", kTag, GetTaskId(), GetFlowActionId(),
             static_cast<int>(params_.type), params_.threshold, params_.threshold_ext);
}

bool AiVideoQuality::Start() {
    if (!AlgActionBase::Start()) {
        return false;
    }
    static_cast<void>(AiSdkInit());
    return true;
}

bool AiVideoQuality::AiSdkInit() {
    action_status = util::ErrorEnum::NotImplement;
    if (!unsupported_reported_.exchange(true, std::memory_order_relaxed)) {
        LOG_WARN("{}[{} {}] Video quality analysis is not implemented; frames will be dropped", kTag,
                 GetTaskId(), GetFlowActionId());
    }
    return false;
}

bool AiVideoQuality::AnalysisKey(const MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            GetTaskId(), GetFlowActionId());
        return false;
    }
    if (param.keys.size() != 3) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] Set {} Failed. key size:{}",
            GetTaskId(), GetFlowActionId(), param.key, param.keys.size());
        return false;
    }

    if (key::AI_PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0] is Not {}",
            GetTaskId(), GetFlowActionId(), key::AI_PARAM);
        return false;
    }

    if (key::diagnosis::VIDEO_DIAGNOSIS != param.keys[1]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0] is Not {}",
            GetTaskId(), GetFlowActionId(), key::diagnosis::VIDEO_DIAGNOSIS);
        return false;
    }

    if (param.keys[2] == key::diagnosis::THRESHOLD) {
        auto value = util::ParseFloat(param.value);
        if ((value >= 0.0f) && (value <= 1.0f)) {
            params_.threshold = value;
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Set {} To {} ",
                GetTaskId(), GetFlowActionId(), param.key, param.value);
        } else {
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Set {} To {} Failed",
                GetTaskId(), GetFlowActionId(), param.key, param.value);
        }
    } else if (param.keys[2] == key::diagnosis::THRESHOLD_EXT) {
        auto value = util::ParseFloat(param.value);
        if ((value >= 0.0f) && (value <= 1.0f)) {
            params_.threshold_ext = value;
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Set {} To {} ",
                GetTaskId(), GetFlowActionId(), param.key, param.value);
        } else {
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Set {} To {} Failed",
                GetTaskId(), GetFlowActionId(), param.key, param.value);
        }
    } else {
        return false;
    }

    return true;
}

// Modify param based on existing
bool AiVideoQuality::ModifyParam(const std::string& /*channel_id*/, const std::string& /*task_id*/,
                                 std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (const auto& param : params) {
        AnalysisKey(param);
    }

    return false;
}

// Set param - clear old, set new fully
bool AiVideoQuality::SetParam(const std::string& /*channel_id*/, const std::string& /*task_id*/,
                              std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear params first
    params_ = {};
    for (const auto& param : params) {
        AnalysisKey(param);
    }

    return false;
}

// Set area - clear old, set new fully
bool AiVideoQuality::SetArea(const std::string& /*channel_id*/, const std::string& /*task_id*/,
                             std::vector<MsgTaskArea>& /*areas*/,
                             std::vector<MsgTaskArea>& /*shielded_areas*/) {
    return true;
}

void AiVideoQuality::HandFrame(AlgDataPtr /*in_data*/) {
    static_cast<void>(AiSdkInit());
}

MsgOverviewMem AiVideoQuality::GetOverviewInfo(const std::string& /*channel_id*/,
                                               const std::string& /*task_id*/, int64_t /*strm_index*/,
                                               int64_t /*from*/, int64_t /*to*/) {
    return {};
}
}  // namespace cosmo
