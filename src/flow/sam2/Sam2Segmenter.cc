// SAM2 segmentation action for video stream pipeline.

#include "flow/sam2/Sam2Segmenter.h"

#include <unistd.h>

#include <algorithm>
#include <thread>

#include "flow/common/AlgDataRecord.h"
#include "media/VideoFrame.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "service/system/IHardwareQuery.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "SAM2-SEGMENTER ";

namespace cosmo {

namespace {
    constexpr size_t kMaxReuseCount = 6;
    constexpr size_t kMaxQueueSize  = 144;
}  // namespace

Sam2Segmenter::~Sam2Segmenter() {
    is_detector_inst_init_ = false;
    LOG_INFO("{}[{} {}] Stop", kTag, alg_code_, uuid);
    Stop();
    if (segmenter_) {
        if (running) {
            running = false;
            stop();
        }

        while (data_queue->RestSize() > 0) {
            data_queue->Pop();
        }

        segmenter_.reset();

        // Clear other resources
        channel_list_.clear();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, alg_code_, uuid);
}

Sam2Segmenter::Sam2Segmenter(ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionSam2Segment, action, "", "",
                    action.atomicCode + " Sam2Segmenter"),
      alg_code_(action.atomicCode) {
    action_status = util::ErrorEnum::ActionReady;
    uuid          = util::GenerateUUID();

    batch_count_     = 1;
    max_reuse_count_ = kMaxReuseCount;
    data_queue->SetMaxSize(kMaxQueueSize);

    LOG_INFO("{}[{} {}] Init MaxReuse:{} BatchCount:{}", kTag, alg_code_, uuid, max_reuse_count_,
             batch_count_);
}

bool Sam2Segmenter::Sam2SdkInit() {
    if (segmenter_) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, alg_code_, uuid);
        return true;
    }

    // ---- Retry limit: after 3 consecutive failures, wait 30 seconds before retrying ----
    constexpr int kMaxRetryBeforeDelay = 3;
    constexpr int64_t kRetryIntervalMs = 30 * 1000;  // 30 seconds

    if (init_retry_count_ >= kMaxRetryBeforeDelay) {
        auto now = util::GetMilliseconds();
        if ((now - last_init_fail_time_ms_) < kRetryIntervalMs) {
            return false;  // Silently skip
        }
        LOG_INFO("{}[{} {}] Init retry after {}s cooldown (retryCount:{})", kTag, alg_code_, uuid,
                 kRetryIntervalMs / 1000, init_retry_count_);
    }

    std::string cfg_path;
    std::string model_path;
    auto cfg_ret = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
        alg_code_, cfg_path, model_path);
    if (!cfg_ret) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", kTag, alg_code_, cfg_ret);
        return false;
    }

    LOG_INFO("{}cfgPath:{}, modelPath:{}", kTag, cfg_path, model_path);

    segmenter_ = std::make_shared<Sam2SegmenterUnify>(alg_code_, cfg_path, model_path);
    auto ret   = segmenter_->Init();
    if (util::ErrorEnum::Success != ret) {
        segmenter_.reset();
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

bool Sam2Segmenter::ValidKey(MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            alg_code_, uuid);
        return false;
    }
    return true;
}

bool Sam2Segmenter::AnalysisKey(const std::string& /*channel_id*/, const std::string& tgt_task_id,
                                MsgDynamicKeyValue& param) {
    std::string key_str = param.key.ToString();
    bool is_input_type = (key_str == "inputType") || (param.keys.size() >= 1 && param.keys[0] == "inputType");
    if (is_input_type) {
        std::string value  = param.value.ToString();
        Sam2InputType type = Sam2InputType::BOX;
        if (value == "point")
            type = Sam2InputType::POINT;
        else if (value == "box")
            type = Sam2InputType::BOX;
        auto it = std::find_if(
            params_.param.begin(), params_.param.end(),
            [&tgt_task_id](const Sam2SegmenterParamEl& el) { return el.task_id == tgt_task_id; });
        if (it != params_.param.end()) {
            it->input_type = type;
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} set inputType: {}",
                alg_code_, uuid, tgt_task_id, value);
        } else {
            Sam2SegmenterParamEl el;
            el.task_id    = tgt_task_id;
            el.input_type = type;
            params_.param.push_back(el);
            LOG_INFO(
                "ModifyParam "
                "[{} {}] Task:{} add param inputType: {}",
                alg_code_, uuid, tgt_task_id, value);
        }
        return true;
    }
    return true;
}

bool Sam2Segmenter::ModifyParam(const std::string& channel_id, const std::string& tgt_task_id,
                                std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        std::string key_str = param.key.ToString();
        bool is_input_type_key =
            (key_str == "inputType") || (param.keys.size() >= 1 && param.keys[0] == "inputType");
        if (is_input_type_key) {
            AnalysisKey(channel_id, tgt_task_id, param);
            continue;
        }
        if (!ValidKey(param))
            continue;
        AnalysisKey(channel_id, tgt_task_id, param);
    }
    params_.param_modify_sign++;
    return true;
}

bool Sam2Segmenter::SetParam(const std::string& channel_id, const std::string& tgt_task_id,
                             std::vector<MsgDynamicKeyValue>& params) {
    Sam2InputType saved_input_type = Sam2InputType::BOX;
    bool has_saved                 = false;
    {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = std::find_if(params_.param.begin(), params_.param.end(),
                               [&tgt_task_id](const auto& p) { return p.task_id == tgt_task_id; });
        if (it != params_.param.end()) {
            saved_input_type = it->input_type;
            has_saved        = true;
        }
        params_.param.clear();
    }
    bool ret = ModifyParam(channel_id, tgt_task_id, params);
    if (has_saved) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = std::find_if(
            params_.param.begin(), params_.param.end(),
            [&tgt_task_id](const Sam2SegmenterParamEl& el) { return el.task_id == tgt_task_id; });
        if (it == params_.param.end()) {
            Sam2SegmenterParamEl el;
            el.task_id    = tgt_task_id;
            el.input_type = saved_input_type;
            params_.param.push_back(el);
        }
    }
    return ret;
}

void Sam2Segmenter::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                                unsigned int duration_sec) {
    AlgActionDataQueueStatus status;
    auto durationInfo = duration_stat.ComputeStats();
    status.durationInfos.push_back(durationInfo);
    if (data_queue->Status(status.queueStatus, duration_sec)) {
        status.actionId = GetActionId();
        for (auto& channel_node : channel_list_) {
            status.channelIds.push_back(channel_node.channel);
            status.taskIds.insert(status.taskIds.end(), channel_node.task_list.begin(),
                                  channel_node.task_list.end());
        }
        status.actionStatus = action_status;
        que_status.push_back(status);
    }
}

bool Sam2Segmenter::AddTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    auto ch_it = std::find_if(channel_list_.begin(), channel_list_.end(),
                              [&channel_id](const auto& ch) { return ch.channel == channel_id; });
    if (ch_it != channel_list_.end()) {
        auto t_it = std::find(ch_it->task_list.begin(), ch_it->task_list.end(), task);
        if (t_it != ch_it->task_list.end()) {
            LOG_WARN("{}[{} {}] Channel:{} Task:{} Already Exist", kTag, alg_code_, uuid, channel_id, task);
            return true;
        }
        ch_it->task_list.push_back(task);
        LOG_INFO("{}[{} {}] Add Task Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id, task);
        return true;
    }

    Sam2SegmenterChannel new_channel;
    new_channel.channel = channel_id;
    new_channel.task_list.push_back(task);
    channel_list_.push_back(new_channel);
    LOG_INFO("{}[{} {}] Add Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id, task);
    return true;
}

// Parameter management — moved to Sam2SegmenterParam.cc

}  // namespace cosmo
