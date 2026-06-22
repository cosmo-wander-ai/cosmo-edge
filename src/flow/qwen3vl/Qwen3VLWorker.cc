// Qwen3VLWorker — Qwen3VLWorker — Qwen3VL vision-language model worker for video stream inference.

#include "flow/qwen3vl/Qwen3VLWorker.h"

#include <unistd.h>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#include "flow/common/AlgDataRecord.h"
#include "flow/common/AlgDataUnit.h"
#include "flow/common/LlmYesNoJudge.h"
#include "media/VideoFrame.h"
#include "service/ai/ILlmInferService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/TimingConstants.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/ClientMsgEvent.h"

static constexpr const char* kTag = "QWEN3VL-WORKER ";

namespace cosmo {

namespace {
    using media::PixelFormat;

    class QwenWorkerActivityGuard {
    public:
        QwenWorkerActivityGuard() {
            service::ServiceRegistry::Instance().Get<service::ILlmInferService>().NotifyWorkerStart();
        }

        ~QwenWorkerActivityGuard() {
            service::ServiceRegistry::Instance().Get<service::ILlmInferService>().NotifyWorkerStop();
        }

        QwenWorkerActivityGuard(const QwenWorkerActivityGuard&)            = delete;
        QwenWorkerActivityGuard& operator=(const QwenWorkerActivityGuard&) = delete;
    };
}  // namespace

// InferEntry struct definition — see Qwen3VLInference.cc

Qwen3VLWorker::~Qwen3VLWorker() {
    detector_inst_init_ = false;
    LOG_INFO("{}[{} {}] Stop", kTag, alg_code_, uuid);
    Stop();
    // Stop() has set running=false and called MvThread::stop() (pthread_join)

    while (data_queue && data_queue->RestSize() > 0) {
        data_queue->Pop();
    }

    // Global shared instance is not released here
    channel_list_.clear();
    LOG_INFO("{}[{} {}] Delete", kTag, alg_code_, uuid);
}

Qwen3VLWorker::Qwen3VLWorker(ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionQwen3VL, action, "", "", action.atomicCode + " Qwen3VLWorker"),
      alg_code_(action.atomicCode) {
    action_status = util::ErrorEnum::ActionReady;
    uuid          = util::GenerateUUID();

    batch_count_     = 1;
    max_reuse_count_ = 6;
    data_queue->SetMaxSize(144);

    LOG_INFO("{}[{} {}] Init MaxReuse:{} BatchCount:{}", kTag, alg_code_, uuid, max_reuse_count_,
             batch_count_);
}

bool Qwen3VLWorker::Qwen3VLSdkInit() {
    // Use global shared Qwen3VL instance
    if (service::ServiceRegistry::Instance().Get<service::ILlmInferService>().IsInitialized()) {
        action_status       = util::ErrorEnum::AI_INST_CREATED;
        detector_inst_init_ = true;
        return true;
    }

    bool ok = service::ServiceRegistry::Instance().Get<service::ILlmInferService>().EnsureInit(alg_code_);
    if (!ok) {
        action_status = util::ErrorEnum::AI_INST_CREATEFAILED;
        LOG_WARN("{}[{} {}] {} Qwen3VL shared instance init failed", kTag, alg_code_, uuid, alg_code_);
        return false;
    }

    action_status = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("{}[{} {}] {} Qwen3VL shared instance ready", kTag, alg_code_, uuid, alg_code_);
    detector_inst_init_ = true;
    return true;
}

// ApplyGenerationStyle, ValidKey, AnalysisKey — moved to Qwen3VLWorkerParam.cc

void Qwen3VLWorker::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                                unsigned int duration_sec) {
    AlgActionDataQueueStatus status;
    auto duration_info = duration_stat.ComputeStats();
    status.durationInfos.push_back(duration_info);
    if (data_queue->Status(status.queueStatus, duration_sec)) {
        status.actionId = GetActionId();
        for (auto& channel_node : channel_list_) {
            status.channelIds.push_back(channel_node.channel);
            status.taskIds.insert(status.taskIds.end(), channel_node.tasks.begin(), channel_node.tasks.end());
        }
        status.actionStatus = action_status;
        que_status.push_back(status);
    }
}

bool Qwen3VLWorker::AddTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    for (auto& ch : channel_list_) {
        if (ch.channel == channel_id) {
            if (std::any_of(ch.tasks.begin(), ch.tasks.end(), [&task](const auto& t) { return t == task; })) {
                LOG_WARN("{}[{} {}] Channel:{} Task:{} Already Exist", kTag, alg_code_, uuid, channel_id,
                         task);
                return true;
            }
            ch.tasks.push_back(task);
            LOG_INFO("{}[{} {}] Add Task Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id, task);
            return true;
        }
    }

    Qwen3VLWorkerChannel new_channel;
    new_channel.channel = channel_id;
    new_channel.tasks.push_back(task);
    channel_list_.push_back(new_channel);
    LOG_INFO("{}[{} {}] Add Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id, task);
    return true;
}

bool Qwen3VLWorker::RemoveTask(const std::string& channel_id, const std::string& task) {
    std::lock_guard<std::shared_mutex> lock(mtx);

    for (auto ch_it = channel_list_.begin(); ch_it != channel_list_.end(); ++ch_it) {
        if (ch_it->channel == channel_id) {
            for (auto task_it = ch_it->tasks.begin(); task_it != ch_it->tasks.end(); ++task_it) {
                if (*task_it == task) {
                    ch_it->tasks.erase(task_it);
                    LOG_INFO("{}[{} {}] Remove Task Channel:{} Task:{}", kTag, alg_code_, uuid, channel_id,
                             task);

                    if (ch_it->tasks.empty()) {
                        channel_list_.erase(ch_it);
                        LOG_INFO("{}[{} {}] Remove Channel:{}", kTag, alg_code_, uuid, channel_id);
                    }
                    task_areas_.erase(task);
                    task_contexts_.erase(task);
                    return true;
                }
            }
        }
    }

    LOG_WARN("{}[{} {}] Remove Task Channel:{} Task:{} Not Found", kTag, alg_code_, uuid, channel_id, task);
    return false;
}

void Qwen3VLWorker::RegisterTaskContext(const std::string& tid, ActionAlgPtr alg, const ActionNode& action) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    action_alg          = alg;
    action_node         = action;
    task_contexts_[tid] = Qwen3VLTaskContext{alg, action};
    LOG_INFO("{}[{} {}] Register task context taskId:{} flowActionId:{} algCode:{}", kTag, alg_code_, uuid,
             tid, action.flowActionId, alg ? alg->algorithmCode : "");
}

bool Qwen3VLWorker::ChannelExist(const std::string& channel_id) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return std::any_of(channel_list_.begin(), channel_list_.end(),
                       [&channel_id](const auto& ch) { return ch.channel == channel_id; });
}

bool Qwen3VLWorker::TaskExist(const std::string& channel_id, const std::string& task) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    for (auto& ch : channel_list_) {
        if (ch.channel == channel_id) {
            return std::any_of(ch.tasks.begin(), ch.tasks.end(),
                               [&task](const auto& t) { return t == task; });
        }
    }
    return false;
}

bool Qwen3VLWorker::TaskIsFull() {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return channel_list_.size() >= max_reuse_count_;
}

bool Qwen3VLWorker::TaskIsEmpty() {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return channel_list_.empty();
}

size_t Qwen3VLWorker::ChannelCount() {
    std::shared_lock<std::shared_mutex> lock(mtx);
    return channel_list_.size();
}

size_t Qwen3VLWorker::TaskCount() {
    std::shared_lock<std::shared_mutex> lock(mtx);
    size_t count = 0;
    for (auto& ch : channel_list_) {
        count += ch.tasks.size();
    }
    return count;
}

Qwen3VLWorkerParamEl Qwen3VLWorker::FoundLocalParamByTask(const AlgTaskUnit& task) {
    Qwen3VLWorkerParamEl param;
    param.task_id = task.task_id;
    return param;
}

Qwen3VLWorkerParamEl Qwen3VLWorker::GetTaskParams(const std::string& tid) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = std::find_if(params_.param.begin(), params_.param.end(),
                           [&tid](const auto& p) { return p.task_id == tid; });
    if (it != params_.param.end())
        return *it;
    if (params_.param.size() == 1)
        return params_.param[0];
    Qwen3VLWorkerParamEl emptyParam;
    return emptyParam;
}

std::optional<Qwen3VLTaskContext> Qwen3VLWorker::GetTaskContext(const std::string& tid) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = task_contexts_.find(tid);
    if (it != task_contexts_.end())
        return it->second;
    if (task_contexts_.size() == 1)
        return task_contexts_.begin()->second;
    return std::nullopt;
}

std::string Qwen3VLWorker::GetTaskFlowActionId(const std::string& tid) {
    auto ctx = GetTaskContext(tid);
    if (ctx)
        return ctx->action_node.flowActionId;
    return action_node.flowActionId;
}

bool Qwen3VLWorker::SetArea(const std::string& /*channel_id*/, const std::string& tid,
                            std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shielded_areas) {
    // Convert area polygon vertices to bounding box (pointBox, normalized coordinates)
    for (auto& area : areas) {
        AreaToLocalBox(area);
    }
    std::lock_guard<std::shared_mutex> lock(mtx);
    auto& taskArea         = task_areas_[tid];
    taskArea.taskId        = tid;
    taskArea.areas         = areas;
    taskArea.shieldedAreas = shielded_areas;

    double pbX = areas.empty() ? 0.0 : areas[0].pointBox.x;
    double pbY = areas.empty() ? 0.0 : areas[0].pointBox.y;
    double pbW = areas.empty() ? 0.0 : areas[0].pointBox.width;
    double pbH = areas.empty() ? 0.0 : areas[0].pointBox.height;

    LOG_INFO("{}[{} {}] SetArea taskId:{} areas:{} shielded:{} pointBox:[{},{},{},{}]", kTag, alg_code_, uuid,
             tid, areas.size(), shielded_areas.size(), pbX, pbY, pbW, pbH);
    return true;
}

// CollectInferEntries, RunBatchInference, DistributeResults, HandFrameBatch — moved to Qwen3VLInference.cc

void Qwen3VLWorker::run() {
    LOG_INFO("{}[{} {}] Thread Start", kTag, alg_code_, uuid);
    QwenWorkerActivityGuard workerActivity;
    try {
        int index = 0;
        while (running) {
            if ((data_queue->RestSize() >= batch_count_) ||
                ((index >= 100) && (data_queue->RestSize() > 0))) {
                if (!running)
                    break;  // Stop signal received, exit immediately
                size_t detnum    = data_queue->RestSize();
                auto input_fps   = input_fps_calc.FpsWithFrame();
                handle_frame_cnt = input_fps.first;
                in_fps           = input_fps.second;
                std::vector<AlgDataPtr> alg_datas;
                for (size_t i = 0; i < detnum && i < batch_count_; i++) {
                    auto data = data_queue->Pop();
                    if (data) {
                        alg_datas.push_back(data);
                    }
                }
                if (!alg_datas.empty()) {
                    if (!running)
                        break;  // Check again before inference
                    duration_stat.BeginSample();
                    HandFrameBatch(alg_datas);
                    duration_stat.EndSample();
                }
                index = 0;
            } else {
                std::this_thread::sleep_for(timing::kFastPollInterval);
                index += 1;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERRO("{}[{} {}] Thread exception: {}", kTag, alg_code_, uuid, e.what());
    } catch (...) {
        LOG_ERRO("{}[{} {}] Thread unknown exception", kTag, alg_code_, uuid);
    }
    LOG_INFO("{}[{} {}] Thread Stop", kTag, alg_code_, uuid);
}
}  // namespace cosmo
