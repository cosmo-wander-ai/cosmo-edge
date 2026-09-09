// AiDetector.cc — Core detection logic: init, inference, filtering, and thread lifecycle.
// Parameter management is in AiDetectorParam.cc.
// Target area marking and history is in AiDetectorTarget.cc.
// Channel/task lifecycle management is in AiDetectorTask.cc.

#include "flow/detect/AiDetector.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IHardwareQuery.h"
#include "util/EnvUtil.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "AI-DETECTER ";
namespace cosmo {

namespace {
    constexpr const char* kEnvMaxReuse     = "COSMO_AI_DETECTOR_MAX_REUSE";
    constexpr const char* kEnvFpsBudget    = "COSMO_AI_DETECTOR_FPS_BUDGET";
    constexpr const char* kEnvReuseProfile = "COSMO_AI_DETECTOR_REUSE_PROFILE";
    constexpr size_t kMaxRuntimeReuseLimit = 64;

    std::string SanitizeEnvSuffix(const std::string& raw) {
        std::string suffix;
        suffix.reserve(raw.size());
        for (unsigned char ch : raw) {
            suffix.push_back(std::isalnum(ch) ? static_cast<char>(std::toupper(ch)) : '_');
        }
        return suffix;
    }

    std::string LookupPlacementEnv(const char* base_key, const std::string& alg_code) {
        const auto suffix = SanitizeEnvSuffix(alg_code);
        if (!suffix.empty()) {
            const auto alg_key = std::string(base_key) + "_" + suffix;
            const auto value   = util::GetEnvOrDefault(alg_key.c_str(), "");
            if (!value.empty()) {
                return value;
            }
        }
        return util::GetEnvOrDefault(base_key, "");
    }

    size_t LookupMaxReuseCount(const std::string& alg_code) {
        const auto raw_value = LookupPlacementEnv(kEnvMaxReuse, alg_code);
        if (raw_value.empty()) {
            return kMaxReuseHardLimit;
        }

        size_t parsed = 0;
        if (!ai_detector_fps::ParsePositiveSize(raw_value, parsed)) {
            LOG_WARN("{}Invalid {} value:{}, fallback:{}", kTag, kEnvMaxReuse, raw_value, kMaxReuseHardLimit);
            return kMaxReuseHardLimit;
        }
        return std::clamp(parsed, static_cast<size_t>(1), kMaxRuntimeReuseLimit);
    }

    ai_detector_fps::ReuseProfile BackendDefaultReuseProfile() {
#ifdef COSMO_NN_USE_RKNN_BACKEND
        return ai_detector_fps::RknnDefaultReuseProfile();
#else
        return ai_detector_fps::DefaultReuseProfile();
#endif
    }

    // Per-algCode fps budget overrides for instance placement. Unlisted algCodes fall back to
    // kDefaultInstanceFpsBudget. Populate from per-model stress-test results.
    float LookupInstanceFpsBudget(const std::string& alg_code) {
        const auto raw_value = LookupPlacementEnv(kEnvFpsBudget, alg_code);
        if (!raw_value.empty()) {
            const float parsed = util::ParseFloat(raw_value, -1.0f);
            if (ai_detector_fps::HasConfiguredFps(parsed)) {
                return parsed;
            }
            LOG_WARN("{}Invalid {} value:{}, fallback:{}", kTag, kEnvFpsBudget, raw_value,
                     kDefaultInstanceFpsBudget);
        }

        static const std::unordered_map<std::string, float> kAlgFpsBudget = {
            // {"45626", 36.0f},
        };
        const auto it = kAlgFpsBudget.find(alg_code);
        return (it != kAlgFpsBudget.end()) ? it->second : kDefaultInstanceFpsBudget;
    }

    ai_detector_fps::ReuseProfile LookupReuseProfile(const std::string& alg_code) {
        const auto raw_value = LookupPlacementEnv(kEnvReuseProfile, alg_code);
        if (raw_value.empty()) {
            return BackendDefaultReuseProfile();
        }

        auto profile = ai_detector_fps::ParseReuseProfile(raw_value);
        if (profile.empty()) {
            LOG_WARN("{}Invalid {} value:{}, fallback to default reuse profile", kTag, kEnvReuseProfile,
                     raw_value);
            return BackendDefaultReuseProfile();
        }
        return profile;
    }

    std::string ReuseProfileToString(const ai_detector_fps::ReuseProfile& profile) {
        if (profile.empty()) {
            return "formula";
        }

        std::ostringstream oss;
        for (size_t i = 0; i < profile.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            oss << profile[i].max_fps << ":" << profile[i].reuse_count;
        }
        return oss.str();
    }
}  // namespace

AiDetector::~AiDetector() {
    is_detector_inst_initialized_ = false;
    LOG_INFO("{}[{} {}] Stop", kTag, name_, uuid);
    AiDetector::Stop();
    if (detector_) {
        if (running) {
            running = false;
            stop();
        }

        while (data_queue->RestSize() > 0) {
            data_queue->Pop();
        }

        detector_.reset();
        detector_ = nullptr;

        channel_list_.clear();
        task_histories_.clear();
        overview_rec_insts_.clear();
        task_areas_.clear();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, name_, uuid);
}

AiDetector::AiDetector(ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiDetect, action, "", "", action.atomicCode + " AiDetector") {
    action_status = util::ErrorEnum::ActionReady;
    data_queue->SetMaxSize(30);
    alg_code_ = action.atomicCode;
    name_     = action.atomicCode;
    uuid      = util::GenerateUUID();

    // Admit the first frame immediately. AiSdkInit replaces this with the
    // loaded model's bounded batch before the next queue drain.
    batch_count_         = 1;
    max_reuse_count_     = LookupMaxReuseCount(alg_code_);
    instance_fps_budget_ = LookupInstanceFpsBudget(alg_code_);
    reuse_profile_       = LookupReuseProfile(alg_code_);
    data_queue->SetMaxSize(48);

    LOG_INFO("{}[{} {}] Init MaxReuse:{} BatchCount:{} FpsBudget:{} ReuseProfile:{}", kTag, name_, uuid,
             max_reuse_count_, batch_count_, instance_fps_budget_, ReuseProfileToString(reuse_profile_));
}

bool AiDetector::AiSdkInit() {
    if (detector_) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, name_, uuid);
        return true;
    }

    constexpr int kMaxRetryBeforeDelay  = 3;
    constexpr int64_t kRetryIntervalMs  = 30 * 1000;
    constexpr int64_t kRequiredGpuMemMB = 512;

    if (init_retry_count_ >= kMaxRetryBeforeDelay) {
        auto now = util::GetMilliseconds();
        if ((now - last_init_fail_time_ms_) < kRetryIntervalMs) {
            return false;
        }
        LOG_INFO("{}[{} {}] Init retry after {}s cooldown (retryCount:{})", kTag, name_, uuid,
                 kRetryIntervalMs / 1000, init_retry_count_);
    }

#ifndef COSMO_NN_USE_HOST_BACKEND
    auto availMB =
        service::ServiceRegistry::Instance().Get<service::IHardwareQuery>().GetAvailableGpuMemoryMB();
    if (availMB >= 0 && availMB < kRequiredGpuMemMB) {
        LOG_WARN("{}[{} {}] Insufficient GPU memory, skipping model load. Available:{}MB Required:{}MB", kTag,
                 name_, uuid, availMB, kRequiredGpuMemMB);
        init_retry_count_++;
        last_init_fail_time_ms_ = util::GetMilliseconds();
        return false;
    }
#else
    static_cast<void>(kRequiredGpuMemMB);
#endif

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet = service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
        alg_code_, cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", kTag, alg_code_, cfgRet);
        return false;
    }
    LOG_INFO("{}cfgPath:{}, modelPath:{}", kTag, cfgPath, modelPath);
    detector_ = std::make_shared<AiDetectorUnify>(alg_code_, cfgPath, modelPath);
    auto ret  = detector_->Init();
    if (util::ErrorEnum::Success != ret) {
        detector_.reset();
        detector_     = nullptr;
        action_status = ret;
        init_retry_count_++;
        last_init_fail_time_ms_ = util::GetMilliseconds();
        LOG_WARN("{}[{} {}] {} Sdk Init Failed Get Ret {} (retryCount:{})", kTag, name_, uuid, alg_code_, ret,
                 init_retry_count_);
        return false;
    }
    init_retry_count_       = 0;
    action_status           = util::ErrorEnum::AI_INST_CREATED;
    labels_                 = detector_->GetLabels();
    size_t model_max_batch  = 1;
    const auto batch_status = detector_->GetMaxBatchSize(model_max_batch);
    if (batch_status != util::ErrorEnum::Success || model_max_batch == 0) {
        LOG_WARN("{}[{} {}] {} invalid model max batch:{}, keep single-frame drain", kTag, name_, uuid,
                 alg_code_, model_max_batch);
        model_max_batch = 1;
    }
    batch_count_ = ai_detector_fps::EffectiveDetectorDrainBatch(ai_detector_fps::kMaxDetectorDrainBatch,
                                                                model_max_batch);
    LOG_INFO("{}[{} {}] {} Init Sdk ModelMaxBatch:{} DrainBatch:{}", kTag, name_, uuid, alg_code_,
             model_max_batch, batch_count_);
    is_detector_inst_initialized_ = true;
    return true;
}

void AiDetector::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status, unsigned int duration_sec) {
    AlgActionDataQueueStatus status;
    auto durationInfo = duration_stat.ComputeStats();
    status.durationInfos.push_back(durationInfo);
    if (data_queue->Status(status.queueStatus, duration_sec)) {
        status.actionId = GetActionId();
        for (auto& channelNode : channel_list_) {
            status.channelIds.push_back(channelNode.channel);
            for (const auto& binding : channelNode.tasks) {
                status.taskIds.push_back(binding.task);
            }
        }
        status.actionStatus = action_status;
        que_status.push_back(status);
    }
    return;
}

void AiDetector::Stop() {
    if (running) {
        running = false;
        if (data_queue) {
            data_queue->Stop();
        }
        stop();
        action_status = util::ErrorEnum::ActionStop;
        LOG_INFO("{}[{} {}] Stop done, thread joined", kTag, name_, uuid);
    }
}

void AiDetector::HandFrames(std::vector<AlgDataPtr> alg_datas) {
    if (0 == alg_datas.size()) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!detector_) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit())
            return;
    }
    std::vector<VideoFramePtr> images;
    std::vector<media::NativeVideoBufferPtr> native_buffers;
    std::vector<AlgDataPtr> activeAlgDatas;
    for (auto algData : alg_datas) {
        if (algData && algData->chanDataDec.frame && algData->chanDataDec.frame->Active()) {
            images.push_back(algData->chanDataDec.frame);
            native_buffers.push_back(algData->chanDataDec.native_buffer);
            activeAlgDatas.push_back(algData);
        }
    }

    if (images.empty()) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    CheckAndActivateConfidence();
    std::vector<AiConfidence> confThres;
    std::vector<std::vector<AiDetectRstEl>> detRsts;

    if (active_confidence_modify_sign_ != active_confidence_active_sign_) {
        active_confidence_active_sign_ = active_confidence_modify_sign_;
        confThres                      = active_confidence_;
        LOG_INFO("{}[{} {}] Confidence Active. confThres size:{}", kTag, name_, uuid, confThres.size());
    }
    action_status = detector_->Detect(images, native_buffers, confThres, detRsts);
    native_buffers.clear();
    if (util::ErrorEnum::Success != action_status) {
        LOG_ERRO("{}[{} {}] Detect Failed. Ret:{} images:{} confThres:{}", kTag, name_, uuid, action_status,
                 images.size(), confThres.size());
        return;
    }
    if (detRsts.size() > activeAlgDatas.size()) {
        LOG_WARN("{}[{} {}] Detect result size:{} larger than active data size:{}, extra results ignored",
                 kTag, name_, uuid, detRsts.size(), activeAlgDatas.size());
    }

    for (size_t i = 0; i < detRsts.size() && i < activeAlgDatas.size(); i++) {
        auto algData                        = activeAlgDatas[i];
        DataDetTrackClassifyPtr detTrackRst = std::make_shared<DataDetTrackClassify>();
        detTrackRst->targets                = detRsts[i];
        detTrackRst->streamIndex            = images[i]->GetStreamIndex();
        detTrackRst->frameIndex             = images[i]->GetFrameIndex();
        detTrackRst->timestamp              = images[i]->GetTimestamp();
        detTrackRst->picWidth               = images[i]->GetWidth();
        detTrackRst->picHeight              = images[i]->GetHeight();
        for (auto& target : detTrackRst->targets) {
            target.frameIndex  = images[i]->GetFrameIndex();
            target.streamIndex = images[i]->GetStreamIndex();
        }
        algData->legacyDetect = algData->chanDataDetect;

        algData->chanDataDetect.atomicCode  = alg_code_;
        algData->chanDataDetect.lables      = labels_;
        algData->chanDataDetect.atomicCodes = {alg_code_};
        algData->chanDataDetect.detRet      = detTrackRst;

        algData->dataType = AlgDataType::ChannelDataDetect;
        distributor->DistributorData(
            algData->channelId, algData,
            [this](AlgDataPtr frame, const std::string& taskId) { return ConfidenceFilter(frame, taskId); });
    }
    return;
}

// When detector is shared by multiple tasks, use lowest confidence for detection
// then filter per-task confidence on distribution
AlgDataPtr AiDetector::ConfidenceFilter(AlgDataPtr data_ptr, const std::string& taskId) {
    AlgDataPtr algData = AlgDataCopy(data_ptr);
    if (!algData)
        return nullptr;

    if (!algData->chanDataDetect.detRet) {
        return data_ptr;
    }

    if (!algData->legacyDetect.atomicCodes.empty()) {
        algData->chanDataDetect.atomicCodes.insert(algData->chanDataDetect.atomicCodes.end(),
                                                   algData->legacyDetect.atomicCodes.begin(),
                                                   algData->legacyDetect.atomicCodes.end());
    }
    algData->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    if (!algData->chanDataDetect.detRet)
        return algData;
    algData->chanDataDetect.detRet->streamIndex = data_ptr->chanDataDetect.detRet->streamIndex;
    algData->chanDataDetect.detRet->frameIndex  = data_ptr->chanDataDetect.detRet->frameIndex;
    algData->chanDataDetect.detRet->timestamp   = data_ptr->chanDataDetect.detRet->timestamp;
    algData->chanDataDetect.detRet->picWidth    = data_ptr->chanDataDetect.detRet->picWidth;
    algData->chanDataDetect.detRet->picHeight   = data_ptr->chanDataDetect.detRet->picHeight;

    algData->chanDataDetect.detRet->dataType = AlgDataType::ChannelDataDetect;

    bool findTask = false;
    for (auto& taskConfidence : active_task_confidence_) {
        if (taskId == taskConfidence.task_id) {
            for (auto& confidenceEl : taskConfidence.label_params) {
                findTask = true;
                for (auto& target : data_ptr->chanDataDetect.detRet->targets) {
                    if ((target.confidence.label == confidenceEl.label) &&
                        (target.confidence.confidence >= confidenceEl.confidence)) {
                        AiDetectRstEl validTarget = target;
                        algData->chanDataDetect.detRet->targets.push_back(validTarget);
                    }
                }
            }
        }
    }

    if (!((!algData->taskId.empty()) && (algData->taskId != taskId))) {
        if (algData->legacyDetect.detRet) {
            algData->chanDataDetect.detRet->targets.insert(algData->chanDataDetect.detRet->targets.end(),
                                                           algData->legacyDetect.detRet->targets.begin(),
                                                           algData->legacyDetect.detRet->targets.end());
        }
    }
    algData->taskId = taskId;
    if (!findTask) {
        algData->chanDataDetect.detRet->targets = data_ptr->chanDataDetect.detRet->targets;
    }

    SignTargetAreas(algData, taskId);
    RecordHistory(algData, taskId);
    OverviewRecord(taskId, algData->chanDataDetect.detRet);
    return algData;
}

// SetProcQueSize — moved to AiDetectorTask.cc

void AiDetector::run() {
    int index = 0;
    while (running) {
        if ((data_queue->RestSize() >= batch_count_) || ((index >= 100) && (data_queue->RestSize() > 0))) {
            size_t detnum     = data_queue->RestSize();
            auto inputFpsCalc = input_fps_calc.FpsWithFrame();
            handle_frame_cnt  = inputFpsCalc.first;
            in_fps            = inputFpsCalc.second;

            std::vector<AlgDataPtr> alg_datas;
            for (size_t i = 0; i < detnum && i < batch_count_; i++) {
                auto data          = data_queue->Pop();
                AlgDataPtr algData = AlgDataCopy(data);
                if (algData) {
                    alg_datas.push_back(algData);
                }
            }
            if (!alg_datas.empty()) {
                if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetActionSwitch(
                        GetActionId())) {
                    duration_stat.BeginSample();
                    HandFrames(alg_datas);
                    duration_stat.EndSample();
                }
            }
            index = 0;
        } else {
            data_queue->WaitForData(10);
            index += 1;
        }
    }
    // Release model on thread exit to free VRAM (AiSdkInit will reload on next start)
    if (detector_) {
        LOG_INFO("{}[{} {}] Thread exiting, releasing YOLO model to free VRAM", kTag, name_, uuid);
        detector_.reset();
        detector_                     = nullptr;
        is_detector_inst_initialized_ = false;
    }
    LOG_INFO("[{} {}] THREAD [{}] Stop ", name_, uuid, Name());
}

// ChannelExist, TaskExist, TaskIsFull, TaskIsEmpty, ChannelCount, TaskCount,
// AddTask, RemoveTask — moved to AiDetectorTask.cc

}  // namespace cosmo
