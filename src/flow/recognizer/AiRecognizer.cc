// AiRecognizer — AiRecognizer — Face/body recognition action with feature comparison.

#include "flow/recognizer/AiRecognizer.h"

#include <unistd.h>

#include "flow/common/FlowTaskUtil.h"
#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IBodyLibService.h"
#include "service/face/IFaceLibService.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/StringUtil.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag = "AI-RECOGNIZER ";

namespace cosmo {

AiRecognizer::AiRecognizer(const std::string& init_task_id, const std::string& alg_code,
                           ActionNode& action_param)
    : AlgActionBase(AlgActionType::AlgActionAiRecognizer, action_param, "", init_task_id),
      AlgDataQueueDistributor(init_task_id + "-" + action_param.actionName + "-" + action_param.flowActionId),
      alg_code_(std::move(alg_code)),
      action_info_(action_param),
      duration_stat_(init_task_id + "-" + action_param.actionName + "-" + action_param.flowActionId),
      overview_rec_inst_(init_task_id, "AiRecognizer_" + alg_code_) {
    action_status = util::ErrorEnum::ActionReady;
    uuid          = util::GenerateUUID();

    for (auto& param : action_param.configObject.params) {
        if (key::FEATURE_INPUT == param.key.ToString()) {
            alg_params_.feature_input = static_cast<FeatureInputType>(util::ParseInt(param.value));
            LOG_INFO("{}[{} {}] Set {} To {} ", kTag, alg_code_, uuid, param.key, param.value);
        } else if (key::MATCH_FLAG == param.key.ToString()) {
            auto value = util::ParseInt(param.value);
            if (!IsValidMatchFlagType(value)) {
                LOG_WARN("{}[{} {}] Set {} To {} Falied", kTag, alg_code_, uuid, param.key, param.value);
                continue;
            }
            alg_params_.match_flag = static_cast<MatchFlagType>(value);
            LOG_INFO("{}[{} {}] Set {} To {} ", kTag, alg_code_, uuid, param.key, param.value);
        }
    }

    LOG_INFO("{}[{} {}] Init feature_input:{} match_flag:{}", kTag, alg_code_, uuid,
             alg_params_.feature_input, alg_params_.match_flag);
}

AiRecognizer::~AiRecognizer() {
    LOG_INFO("{}[{} {}] Stop", kTag, alg_code_, uuid);
    Stop();
    std::lock_guard<std::shared_mutex> lock(mtx_);
    inst_.reset();
    LOG_INFO("{}[{} {}] Delete", kTag, alg_code_, uuid);
}

bool AiRecognizer::AiSdkInit() {
    if (inst_) {
        return true;
    }

    std::string cfg_path;
    std::string model_path;
    auto cfg_ret = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
        alg_code_, cfg_path, model_path);
    if (!cfg_ret) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} ret:{}", kTag, alg_code_, cfg_ret);
        return false;
    }

    auto pool =
        service::ServiceRegistry::Instance().Get<service::IInferPoolService>().GetRecognizerPool(alg_code_);
    inst_ = std::make_shared<AiRecognizerInterface>(pool, alg_code_, cfg_path, model_path);

    action_status = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("{}[{} {}] Init Sdk", kTag, alg_code_, uuid);
    return true;
}

bool AiRecognizer::RegistTaskQueue(AlgTaskUnit& param) {
    return RegistProcQueue(param);
}

bool AiRecognizer::RemoveTaskQueue(AlgTaskUnit& param) {
    return RemoveProcQueue(param);
}

int AiRecognizer::ForceRemoveByTaskId(const std::string& taskId) {
    // AiRecognizer inherits AlgDataQueueDistributor separately, so clean both:
    // 1. The inherited AlgDataQueueDistributor (where child queues are registered)
    int count = AlgDataQueueDistributor::ForceRemoveByTaskId(taskId);
    // 2. The base class AlgActionBase distributor
    count += AlgActionBase::ForceRemoveByTaskId(taskId);
    return count;
}

void AiRecognizer::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status, unsigned int duration_sec) {
    AlgActionDataQueueStatus status;
    status.durationInfos.push_back(duration_stat_.ComputeStats());
    if (data_queue && data_queue->Status(status.queueStatus, duration_sec)) {
        status.actionId = GetActionId();
        status.taskIds.push_back(task_id);
        status.actionStatus = action_status;
        que_status.push_back(status);
    }
}

void AiRecognizer::ActionInfo(std::vector<ActionRuntimeInfo>& action_infos) {
    ActionRuntimeInfo action_info_el;
    action_info_el.actionId = GetActionId();
    auto bind_tasks         = GetBindTasks();
    for (auto& bind_task : bind_tasks) {
        for (auto& task : bind_task.tasks) {
            ActionRuntimeSon son;
            son.channelId = task.channel_id;
            son.taskId    = task.task_id;
            son.actionId  = task.actionId;
            action_info_el.sons.push_back(son);
        }
    }
    action_infos.push_back(action_info_el);
}

bool AiRecognizer::AnalysisKey(MsgDynamicKeyValue& param) {
    const std::string key_str(util::Trim(param.key.ToRefString()));

    const bool is_face_set_key =
        (key_str == "param.faceSet") ||
        (param.keys.size() == 2 && param.keys[0] == key::PARAM && param.keys[1] == key::target::FACE_SET);
    // Work clothes param.workClothesSet shares the same comparison list with param.faceSet
    const bool is_work_clothes_set_key =
        (key_str == "param.workClothesSet") || (param.keys.size() == 2 && param.keys[0] == key::PARAM &&
                                                param.keys[1] == key::target::PARAM_WORKCLOTHES_SET);
    if (is_face_set_key || is_work_clothes_set_key) {
        const auto v = util::Trim(param.value.ToRefString());
        if (v.empty()) {
            return false;
        }
        params_.face_set.clear();
        for (auto tok : util::Split(v, ",")) {
            const auto t = util::Trim(tok);
            if (!t.empty()) {
                params_.face_set.emplace_back(std::string(t));
            }
        }
        LOG_INFO(
            "ModifyParam "
            "[{} {}] Set {} To {}",
            alg_code_, uuid, param.key, param.value);
        return true;
    }

    const bool is_limit_score_key =
        (key_str == "param.limitScore") ||
        (param.keys.size() == 2 && param.keys[0] == key::PARAM && param.keys[1] == key::target::LIMIT_SCORE);
    if (is_limit_score_key) {
        auto value = util::ParseFloat(param.value);
        if ((value < 0.0f) || (value > 100.0f)) {
            LOG_WARN(
                "ModifyParam "
                "[{} {}] Set {} To {} Failed",
                alg_code_, uuid, param.key, param.value);
            return false;
        }
        alg_params_.limit_score = value;
        LOG_INFO(
            "ModifyParam "
            "[{} {}] Set {} To {}",
            alg_code_, uuid, param.key, param.value);
        return true;
    }

    return false;
}

bool AiRecognizer::ModifyParam(const std::string& /*channel_id*/, const std::string& /*task_id*/,
                               std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto& p : params) {
        AnalysisKey(p);
    }
    return false;
}

bool AiRecognizer::SetParam(const std::string& /*channel_id*/, const std::string& /*task_id*/,
                            std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    params_ = {};
    for (auto& p : params) {
        AnalysisKey(p);
    }
    return false;
}

bool AiRecognizer::SetArea(const std::string& /*channel_id*/, const std::string& area_task_id,
                           std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shielded_areas) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    task_area_.taskId        = area_task_id;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shielded_areas;
    for (auto& area : task_area_.areas) {
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& assArea : area.associatedAreas) {
            assArea.iderectionType = GetDirectionTypeFromMsg(assArea.params);
        }
    }
    return true;
}

bool AiRecognizer::GetRecodResult(bool compare_rst, size_t count, AiDetectMatchHighScoreInfo& match_info) {
    if (count == 0) {
        LOG_INFO("[{} {}] Set Have No Target", alg_code_, uuid);
        action_status = util::ErrorEnum::DependLibEmpty;
        return false;
    }

    bool rst = false;
    if (MatchFlagType::Match == alg_params_.match_flag) {
        rst = compare_rst;
    } else if (MatchFlagType::NotMatch == alg_params_.match_flag) {
        rst = !compare_rst;
    } else {
        rst = true;
    }
    action_status = util::ErrorEnum::Success;
    LOG_INFO("[{} {}] compare Size:{} Get Rst:{} max matchScore:{} actureRet:{}", alg_code_, uuid, count,
             compare_rst, match_info.match_degree, rst);
    return rst;
}

void AiRecognizer::HandFace(AlgDataPtr alg_data) {
    // Determine the upstream output data type based on feature_input:
    // Face pipeline uses AiLandmark -> TaskDataLandmark
    // Work clothes/Body pipeline uses classifier -> TaskDataClassify
    AlgDataType expected_type = AlgDataType::TaskDataLandmark;
    if (alg_params_.feature_input == FeatureInputType::Body ||
        alg_params_.feature_input == FeatureInputType::Things ||
        alg_params_.feature_input == FeatureInputType::BodyNoCompare) {
        expected_type = AlgDataType::TaskDataClassify;
    }

    if (expected_type != alg_data->dataType) {
        bool can_use_area_fallback = !FlowHasUpstreamTargetSource(action_alg, action_node.flowActionId) &&
                                     (alg_params_.feature_input == FeatureInputType::Body ||
                                      alg_params_.feature_input == FeatureInputType::Things ||
                                      alg_params_.feature_input == FeatureInputType::BodyNoCompare);
        if (!can_use_area_fallback || alg_data->dataType != AlgDataType::ChannelDataDec) {
            action_status = util::ErrorEnum::FlowDataInvalid;
            return;
        }
    }

    auto input                      = alg_data->GetTaskResult(expected_type);
    bool has_upstream_target_source = FlowHasUpstreamTargetSource(action_alg, action_node.flowActionId);
    if (!input && has_upstream_target_source) {
        return;
    }

    std::vector<AiDetectRstEl> fallback_targets;
    if ((!input || input->targets.empty()) && !has_upstream_target_source &&
        (alg_params_.feature_input == FeatureInputType::Body ||
         alg_params_.feature_input == FeatureInputType::Things ||
         alg_params_.feature_input == FeatureInputType::BodyNoCompare)) {
        std::vector<MsgTaskArea> areas;
        {
            std::shared_lock<std::shared_mutex> lock(mtx_);
            areas = task_area_.areas;
        }
        fallback_targets = BuildAreaFallbackTargets(areas, pic_width_, pic_height_);
        if (!fallback_targets.empty()) {
            input              = std::make_shared<DataDetTrackClassify>();
            input->targets     = fallback_targets;
            input->bHaveArea   = true;
            input->frameIndex  = alg_data->chanDataDec.frame->GetFrameIndex();
            input->streamIndex = alg_data->chanDataDec.frame->GetStreamIndex();
            input->timestamp   = alg_data->chanDataDec.frame->GetTimestamp();
            input->picWidth    = pic_width_;
            input->picHeight   = pic_height_;
            alg_data->SetTaskResult(expected_type, input);
            alg_data->dataType = expected_type;
        }
    }
    if (!input || input->targets.empty()) {
        return;
    }

    duration_stat_.BeginSample();
    bool use_box  = (expected_type != AlgDataType::TaskDataLandmark);
    action_status = inst_->Recognize(alg_data->chanDataDec.frame, input->targets, use_box);
    duration_stat_.EndSample();

    alg_data->taskDataAlarm.alarmData = std::make_shared<DataAlarm>();
    if (!alg_data->taskDataAlarm.alarmData) {
        return;
    }

    for (size_t index = 0; index < input->targets.size(); index++) {
        // Skip targets filtered by TargetFilter size/confidence filter (e.g., face smaller than minimum size)
        // Note: Only skip if filterType==TargetFilter. "Have No Label" targets still need to be processed
        if (input->targets[index].bFilter && input->targets[index].filterType == AIFilterType::TargetFilter) {
            continue;
        }
        if (input->targets[index].feature.feature.empty()) {
            continue;
        }

        AiDetectMatchHighScoreInfo match_info;
        bool matched = false;

        // Body/Things comparison via IBodyLibService (cached)
        if (alg_params_.feature_input == FeatureInputType::Body ||
            alg_params_.feature_input == FeatureInputType::Things) {
            auto compare_fn = [this](const AiFeature& f1, const AiFeature& f2) -> float {
                return inst_->CompareFeature(f1, f2);
            };
            matched = service::ServiceRegistry::Instance().Get<service::IBodyLibService>().BodyCompare(
                params_.face_set, input->targets[index].feature, match_info, alg_params_.limit_score,
                compare_fn);
        } else {
            // Face uses existing FaceManager for comparison
            matched = service::ServiceRegistry::Instance().Get<service::IFaceFeature>().FaceCompare(
                params_.face_set, input->targets[index].feature, match_info, alg_params_.limit_score);
        }

        if (!GetRecodResult(matched, params_.face_set.size(), match_info)) {
            continue;
        }

        for (auto area : input->targets[index].areaSign.areas) {
            DataAlarmUnit unit;
            unit.flowActionId = action_node.flowActionId;
            unit.areaId       = area.area_id;
            unit.areaName     = area.area_name;
            unit.trackId      = input->targets[index].trackId;
            unit.strTrackId   = input->targets[index].trackIdInfo;
            unit.box          = input->targets[index].box;
            unit.boxs.push_back(unit.box);
            unit.haveRelated = input->targets[index].relatedEl.bActive;
            unit.relatedBox  = input->targets[index].relatedEl.box;
            unit.feature     = input->targets[index].feature;
            unit.confidence  = unit.haveRelated ? input->targets[index].relatedEl.classifyRst
                                                : input->targets[index].classifyRst;
            unit.reportType  = OnEventsReportType::Realtime;
            unit.matchInfo   = match_info;
            alg_data->taskDataAlarm.alarmData->alarms.push_back(unit);
        }
    }

    if (alg_data->taskDataAlarm.alarmData->alarms.empty()) {
        return;
    }

    // Populate taskDatarecog.areas for the sensitivity component
    // Sensitivity::AddAreaThingsHistory reads data from this field to calculate sensitivity
    for (auto& alarm : alg_data->taskDataAlarm.alarmData->alarms) {
        AlgTaskDataRecogThings recog_area;
        recog_area.areaId       = alarm.areaId;
        recog_area.areaName     = alarm.areaName;
        recog_area.bLogicResult = true;  // Passed GetRecodResult check
        recog_area.box          = alarm.box;
        recog_area.matchInfo    = alarm.matchInfo;
        alg_data->taskDatarecog.areas.push_back(recog_area);
    }

    alg_data->taskDataAlarm.alarmData->multiAlarms += 1;
    alg_data->dataType = AlgDataType::TaskDataRecognizer;
    DistributorData(alg_data);
}

void AiRecognizer::HandFrame(AlgDataPtr alg_data) {
    if (!alg_data) {
        filter_frames_ += 1;
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    if (!inst_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        std::lock_guard<std::shared_mutex> lock(mtx_);
        if (!inst_ && !AiSdkInit()) {
            return;
        }
    }
    if (!alg_data->chanDataDec.frame) {
        return;
    }

    pic_width_  = alg_data->chanDataDec.frame->GetWidth();
    pic_height_ = alg_data->chanDataDec.frame->GetHeight();
    HandFace(alg_data);
}

MsgOverviewMem AiRecognizer::GetOverviewInfo(const std::string& /*channel_id*/,
                                             const std::string& /*overview_task_id*/,
                                             int64_t query_stream_index, int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(query_stream_index, from, to);
}

}  // namespace cosmo
