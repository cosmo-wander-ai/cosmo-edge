// AI classifier action base class implementation.

#include "flow/classify/AiClassifierBase.h"

#include "flow/common/AlgDataRecord.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelService.h"
#include "service/system/IConfigReadService.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {

AiClassifierBase::AiClassifierBase(AlgActionType actionType, const std::string& taskId, ActionNode& action,
                                   const std::string& logTag, const std::string& overviewRecordKey)
    : AlgActionBase(actionType, action, "", taskId),
      log_tag_(logTag),
      alg_code_(action.atomicCode),
      overview_rec_inst_(taskId, overviewRecordKey) {
    action_status = util::ErrorEnum::ActionReady;
    uuid          = util::GenerateUUID();
}

AiClassifierBase::~AiClassifierBase() {
    LOG_INFO("{}[{} {} {}] Stop", log_tag_, GetTaskId(), GetName(), uuid);
    Stop();
    if (classifier_) {
        classifier_.reset();
        classifier_ = nullptr;
    }
    LOG_INFO("{}[{} {} {}] Delete", log_tag_, GetTaskId(), GetName(), uuid);
}

bool AiClassifierBase::AiSdkInit() {
    // Return early if SDK is already initialized.
    if (classifier_) {
        LOG_INFO("{}[{} {} {}] Sdk Have Init", log_tag_, GetTaskId(), GetName(), uuid);
        return true;
    }

    std::string cfg_path   = "";
    std::string model_path = "";
    auto cfg_ret           = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
                  alg_code_, cfg_path, model_path);
    if (!cfg_ret) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", log_tag_, alg_code_, cfg_ret);
        return false;
    }
    LOG_INFO("{}cfgPath:{}, modelPath:{}", log_tag_, cfg_path, model_path);
    classifier_ = std::make_shared<AiClassifierUnify>(alg_code_, cfg_path, model_path);
    auto ai_ret = classifier_->Init();
    if (util::ErrorEnum::Success != ai_ret) {
        classifier_.reset();
        classifier_   = nullptr;
        action_status = ai_ret;
        LOG_WARN("{}[{} {} {}] Sdk Init Failed code:{}", log_tag_, GetTaskId(), GetName(), uuid, ai_ret);
        return false;
    }
    action_status = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("{}[{} {} {}] Init Sdk", log_tag_, GetTaskId(), GetName(), uuid);
    return true;
}

void AiClassifierBase::RecordHistory(AlgDataPtr dataPtr) {
    if (!dataPtr)
        return;
    auto classify_result = dataPtr->GetTaskResult(AlgDataType::TaskDataClassify);
    if (!classify_result)
        return;

    std::lock_guard<std::shared_mutex> lock(mtx);
    historys_.push_back(*classify_result);
    if (classify_result->timestamp - historys_.front().timestamp > media::kVideoInfoMaxDuration) {
        historys_.pop_front();
    }

    overview_rec_inst_.OverviewRecordFrame(classify_result);
}

MsgOverviewMem AiClassifierBase::GetOverviewInfo(const std::string& /*channelId*/,
                                                 const std::string& /*taskId*/, int64_t streamIndex,
                                                 int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(streamIndex, from, to);
}

bool AiClassifierBase::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                               std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& area : areas) {
        AreaToLocalBox(area);
    }
    task_area_.taskId        = taskId;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shieldedAreas;
    return true;
}

std::vector<DataDetTrackClassify> AiClassifierBase::GetHistory(const std::string& /*channelId*/,
                                                               const std::string& /*taskId*/, int64_t from,
                                                               int64_t ts, int64_t to) {
    std::vector<DataDetTrackClassify> rst;
    std::shared_lock<std::shared_mutex> lock(mtx);
    bool is_start = false;

    for (auto& history_el : historys_) {
        auto timestamp_diff = (ts == 0) ? 0 : abs(ts - history_el.timestamp);
        // Used for alert retrieval; the start frame (I-frame) may not exist exactly.
        if ((from <= history_el.frameIndex) && (timestamp_diff < media::kTimestampDiff)) {
            is_start = true;
        }
        if (is_start) {
            history_el.dataType = AlgDataType::TaskDataClassify;
            rst.push_back(history_el);
            if (to <= history_el.frameIndex)  // End frame may not exist exactly
            {
                break;
            }
        }
    }

    return rst;
}

void AiClassifierBase::SetProcQueSize() {
    size_t que_max_size  = 64;
    size_t que_size_base = 10;
    auto que_in_fps      = data_queue->GetInFps();
    if (que_in_fps <= 5.5f) {
        que_max_size = que_size_base * 2 * media::kQueueSizeCoefficient;
    } else if (que_in_fps <= 10.5f) {
        que_max_size = que_size_base * 5 * media::kQueueSizeCoefficient;
    } else {
        que_max_size = que_size_base * 10 * media::kQueueSizeCoefficient;
    }

    data_queue->SetMaxSize(que_max_size);
}

void AiClassifierBase::run() {
    while (running) {
        if (data_queue->RestSize() >= batch_count_) {
            auto input_fps_val = input_fps_calc.FpsWithFrame();
            handle_frame_cnt.store(input_fps_val.first, std::memory_order_relaxed);
            in_fps.store(input_fps_val.second, std::memory_order_relaxed);
            SetProcQueSize();

            std::vector<AlgDataPtr> alg_datas;
            for (size_t i = 0; i < batch_count_; i++) {
                auto dec_data = data_queue->Pop();
                if (CheckDataAvailable(dec_data)) {
                    alg_datas.push_back(dec_data);
                }
            }

            if (!alg_datas.empty()) {
                if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetActionSwitch(
                        GetActionId())) {
                    duration_stat.BeginSample();
                    HandFramesEx(alg_datas);
                    duration_stat.EndSample();
                }
            }

        } else {
            data_queue->WaitForData(10);
        }
    }

    LOG_INFO("{}[{} {} {}] THREAD [{}] Stop ", log_tag_, GetTaskId(), GetName(), uuid, Name());
}

}  // namespace cosmo
