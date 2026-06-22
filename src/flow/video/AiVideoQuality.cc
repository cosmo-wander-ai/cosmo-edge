// AI Video Quality Action.

#include "flow/video/AiVideoQuality.h"

#include <mutex>
#include <shared_mutex>

#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag = "AI-VIDEOQUALITY ";

namespace cosmo {

namespace {
    constexpr int kFilterLogInterval = 100;
}  // namespace

AiVideoQuality::~AiVideoQuality() {
    LOG_INFO("{}[{} {}] Stop", kTag, GetTaskId(), GetFlowActionId());
    Stop();
    if (inst_) {
        inst_.reset();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, GetTaskId(), GetFlowActionId());
}

AiVideoQuality::AiVideoQuality(const std::string& task, ActionNode& action_param)
    : AlgActionBase(AlgActionType::AlgActionAiVideoQuality, action_param, "", task),
      action_info_(action_param),
      overview_rec_inst_(task, "aiVideoQuality_" + action_param.flowActionId) {
    action_status = util::ErrorEnum::ActionReady;
    for (const auto& action_param_iter : action_param.configObject.params) {
        if (key::diagnosis::TYPE == action_param_iter.key.ToString()) {
            auto value = util::ParseInt(action_param_iter.value);
            if (infer::IsValidVideoQualityType(value)) {
                params_.type = static_cast<infer::AiVideoQualityType>(value);
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

bool AiVideoQuality::AiSdkInit() {
    // Check error state, then initialize SDK
    if (inst_) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, GetTaskId(), GetFlowActionId());
        return true;
    }

    inst_ = std::make_shared<infer::AiVideoQualityUnify>();
    if (util::ErrorEnum::Success != inst_->Init(params_.type)) {
        action_status = util::ErrorEnum::AI_INST_CREATEFAILED;
        LOG_WARN("{}[{} {}] Sdk Init Failed", kTag, GetTaskId(), GetFlowActionId());
        return false;
    }
    inst_->SetThreshold(params_.threshold, params_.threshold_ext);
    action_status = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("{}[{} {}] Init Sdk", kTag, GetTaskId(), GetFlowActionId());
    return true;
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
            params_.threshold        = value;
            params_.is_param_changed = true;
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
            params_.threshold_ext    = value;
            params_.is_param_changed = true;
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
bool AiVideoQuality::SetArea(const std::string& /*channel_id*/, const std::string& target_task_id,
                             std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shielded_areas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    task_area_.taskId        = target_task_id;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shielded_areas;
    for (auto& area : task_area_.areas) {
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& ass_area : area.associatedAreas) {
            ass_area.iderectionType = GetDirectionTypeFromMsg(ass_area.params);
        }
    }
    return true;
}

std::vector<MsgTaskArea> AiVideoQuality::GetAssoAreas(const std::vector<MsgTaskArea>& areas) const {
    std::vector<MsgTaskArea> asso_area = areas;
    for (const auto& area : areas) {
        if (area.bHaveAssoArea) {
            auto asso_unit = GetAssoAreas(area.associatedAreas);
            asso_area.insert(asso_area.end(), asso_unit.begin(), asso_unit.end());
        }
    }
    return asso_area;
}

TaskBaseArea AiVideoQuality::GetArea() {
    TaskBaseArea ret_area;
    std::shared_lock<std::shared_mutex> lock(mtx);

    ret_area = task_area_;
    return ret_area;
}

void AiVideoQuality::HandFrame(AlgDataPtr in_data) {
    auto alg_data = AlgDataCopy(in_data);
    if (!alg_data) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{} {}] Filter {} Frames", kTag, GetTaskId(), GetFlowActionId(), invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    if (!inst_) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{} {}] Filter {} Frames", kTag, GetTaskId(), GetFlowActionId(), invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit()) {
            return;
        }
    }
    if (!alg_data->chanDataDec.frame) {
        return;
    }

    pic_width_  = static_cast<int>(alg_data->chanDataDec.frame->GetWidth());
    pic_height_ = static_cast<int>(alg_data->chanDataDec.frame->GetHeight());

    if (params_.is_param_changed) {
        params_.is_param_changed = false;
        inst_->SetThreshold(params_.threshold, params_.threshold_ext);
    }

    float score{-1.0f};
    auto ret = inst_->Analysis(alg_data->chanDataDec.frame, score);

    AiDetectRstEl target;
    target.box.x        = 0;
    target.box.y        = 0;
    target.box.width    = pic_width_;
    target.box.height   = pic_height_;
    target.frameIndex   = static_cast<int64_t>(alg_data->chanDataDec.frame->GetFrameIndex());
    target.streamIndex  = alg_data->chanDataDec.frame->GetStreamIndex();
    target.bLogicResult = ret;
    AiConfidence confidence;
    confidence.label      = "VideoQuality";
    confidence.confidence = score;
    auto conf_area        = GetArea();
    for (auto& area : conf_area.areas) {
        TargetAreaUnit target_area;
        target_area.area_id   = area.areaId;
        target_area.area_name = area.name;
        target.areaSign.areas.push_back(target_area);
    }
    target.classifyRst.push_back(confidence);
    auto vq_result = std::make_shared<DataDetTrackClassify>();
    alg_data->SetTaskResult(AlgDataType::TaskDataAiVideoQuality, vq_result);

    vq_result->streamIndex = target.streamIndex;
    vq_result->frameIndex  = target.frameIndex;
    vq_result->timestamp   = alg_data->chanDataDec.frame->GetTimestamp();
    vq_result->picWidth    = pic_width_;
    vq_result->picHeight   = pic_height_;
    vq_result->dataType    = AlgDataType::TaskDataAiVideoQuality;
    vq_result->targets.push_back(target);
    alg_data->dataType      = AlgDataType::TaskDataAiVideoQuality;
    alg_data->bHaveClassify = true;  // For sensitivity logic

    // Distribute queue
    distributor->DistributorData(alg_data);
}

MsgOverviewMem AiVideoQuality::GetOverviewInfo(const std::string& /*channel_id*/,
                                               const std::string& /*task_id*/, int64_t strm_index,
                                               int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(strm_index, from, to);
}
}  // namespace cosmo
