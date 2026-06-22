// LogicalJudgment — Logical Judgment implementation.

#include "flow/logical/LogicalJudgment.h"

#include "flow/common/AlgDataRecord.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag    = "LogicalJudgment ";
static constexpr size_t kLogInterval = 100;
namespace cosmo {

LogicalJudgment::~LogicalJudgment() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

LogicalJudgment::LogicalJudgment(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBALogicalJudgment, action, "", taskId),
      logic_(action.configObject.condition),
      calc_engine_(
          taskId,
          [this](const std::string& label, float& value) -> bool {
              for (auto& p : params_.params) {
                  if (p.label == label) {
                      value = p.value;
                      return true;
                  }
              }
              return false;
          },
          [this](const std::string& key, const std::string& value) -> bool {
              for (auto& c : params_.customs) {
                  if (c.key == key) {
                      return std::find(c.values.begin(), c.values.end(), value) != c.values.end();
                  }
              }
              return false;
          }),
      overview_rec_inst_(taskId, "logic") {
    action_status = util::ErrorEnum::ActionReady;
    LOG_INFO("{}Task:{} Init, logic.type:{} KeyL:{} KeyR:{}", kTag, task_id, logic_.type, logic_.keyL,
             logic_.keyR);
}

/*
aiParam.falldown.confidence
*/
bool LogicalJudgment::AnalysisKey(MsgDynamicKeyValue& param, LogicalJudgmentLogicParam& localParamEl) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            task_id);
        return false;
    }
    if (param.keys.size() != 3) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. key size:{}",
            task_id, param.key, param.keys.size());
        return false;
    }

    if ((key::AI_PARAM != param.keys[0]) || (key::CONFIDENCE != param.keys[2])) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] param.keys[0] is Not {}",
            task_id, key::PARAM);
        return false;
    }

    localParamEl.label = param.keys[1];
    localParamEl.value = util::ParseFloat(param.value.ToString());
    LOG_INFO(
        "ModifyParam "
        "[{}] Set label:{} value:{} ",
        task_id, localParamEl.label, localParamEl.value);

    return true;
}

/*
custom.detectLabels
*/
bool LogicalJudgment::AnalysisCustomKey(MsgDynamicKeyValue& param, MsgDynamicKeyValue& localParamEl) {
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

    if (key::CUSTOM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] param.keys[0] is Not {}",
            task_id, key::CUSTOM);
        return false;
    }

    localParamEl.key   = param.key;
    localParamEl.value = param.value;
    auto values        = util::Split(param.value.ToRefString(), ",");
    localParamEl.values.assign(values.begin(), values.end());

    LOG_INFO(
        "ModifyParam "
        "[{}] Set key:{} value:{} size:{}",
        task_id, localParamEl.key, localParamEl.value, localParamEl.values.size());

    return true;
}

// Modify parameters based on existing ones
bool LogicalJudgment::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                  std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        LogicalJudgmentLogicParam localNewParamEl;
        if (AnalysisKey(param, localNewParamEl)) {
            bool findIt = false;
            for (auto& localParamEl : params_.params) {
                if (localParamEl.label == localNewParamEl.label) {
                    localParamEl.value = localNewParamEl.value;
                    findIt             = true;
                }
            }
            if (!findIt) {
                params_.params.push_back(localNewParamEl);
            }
        }

        MsgDynamicKeyValue localNewKeyValueEl;
        if (AnalysisCustomKey(param, localNewKeyValueEl)) {
            bool findIt = false;
            for (auto& localParamEl : params_.customs) {
                if (localParamEl.key == localNewKeyValueEl.key) {
                    localParamEl.value  = localNewKeyValueEl.value;
                    localParamEl.values = localNewKeyValueEl.values;
                    localParamEl.keys   = localNewKeyValueEl.keys;
                    findIt              = true;
                }
            }
            if (!findIt) {
                params_.customs.push_back(localNewKeyValueEl);
            }
        }
    }

    return false;
}

// Set parameters - clear previous ones and set fully new ones
bool LogicalJudgment::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                               std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    params_.params.clear();
    params_.customs.clear();
    for (auto& param : params) {
        LogicalJudgmentLogicParam localParamEl;
        if (AnalysisKey(param, localParamEl)) {
            params_.params.push_back(localParamEl);
        }

        MsgDynamicKeyValue localParamKeyValueEl;
        if (AnalysisCustomKey(param, localParamKeyValueEl)) {
            params_.customs.push_back(localParamKeyValueEl);
        }
    }

    return false;
}

bool LogicalJudgment::LogicTest(AiDetectRstEl& target) {
    return calc_engine_.GetLogicResult(target, logic_, true);
}

void LogicalJudgment::HandFrame(AlgDataPtr algData) {
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kLogInterval) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!((AlgDataType::ChannelDataDetect == algData->dataType) ||
          (AlgDataType::TaskDataTrack == algData->dataType) ||
          (AlgDataType::TaskDataClassify == algData->dataType) ||
          (AlgDataType::TaskDataGroupClassify == algData->dataType) ||
          (AlgDataType::TaskDataClassifyMultPic == algData->dataType))) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kLogInterval) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    DataDetTrackClassifyPtr input;
    if (AlgDataType::ChannelDataDetect == algData->dataType) {
        input = algData->chanDataDetect.detRet;
    } else if (AlgDataType::TaskDataTrack == algData->dataType) {
        input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
    } else if (AlgDataType::TaskDataClassifyMultPic == algData->dataType) {
        input = algData->taskDataClassifyMultPic.classifyRst;
    } else {
        input = algData->GetTaskResult(AlgDataType::TaskDataClassify);
    }

    for (auto& target : input->targets) {
        if (!target.bFilter)
            target.bLogicResult = calc_engine_.GetLogicResult(target, logic_);
    }
    overview_rec_inst_.OverviewRecordFrame(input);

    handle_frame_cnt += 1;
    if (0 == handle_frame_cnt % kLogInterval) {
        LOG_INFO("{}[{}] Handle {} Frames", kTag, task_id, handle_frame_cnt);
    }
    action_status = util::ErrorEnum::Success;
    distributor->DistributorData(algData);
}

MsgOverviewMem LogicalJudgment::GetOverviewInfo(const std::string& /*channelId*/,
                                                const std::string& /*taskId*/, int64_t streamIndex,
                                                int64_t from, int64_t to) {
    return overview_rec_inst_.GetOverviewInfo(streamIndex, from, to);
}

}  // namespace cosmo