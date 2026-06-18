// AiDetector.cc — Core detection logic: init, inference, filtering, and thread lifecycle.
// Parameter management is in AiDetectorParam.cc.
// Target area marking and history is in AiDetectorTarget.cc.
// Channel/task lifecycle management is in AiDetectorTask.cc.

#include "flow/detect/AiDetector.h"

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IHardwareQuery.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

static constexpr const char* kTag = "AI-DETECTER ";
namespace cosmo {

AiDetector::~AiDetector() {
    m_detectorInstInit = false;
    LOG_INFO("{}[{} {}] Stop", kTag, m_name, uuid);
    AiDetector::Stop();
    if (m_detector) {
        if (running) {
            running = false;
            stop();
        }

        while (data_queue->RestSize() > 0) {
            data_queue->Pop();
        }

        m_detector.reset();
        m_detector = nullptr;

        m_channelList.clear();
        m_taskHistorys.clear();
        m_overviewRecInsts.clear();
        m_taskAreas.clear();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, m_name, uuid);
}

AiDetector::AiDetector(ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiDetect, action, "", "", action.atomicCode + " AiDetector") {
    action_status = util::ErrorEnum::ActionReady;
    data_queue->SetMaxSize(30);
    m_algCode = action.atomicCode;
    m_name    = action.atomicCode;
    uuid      = util::GenerateUUID();

    m_batchCount    = 4;
    m_maxReuseCount = 6;
    data_queue->SetMaxSize(48);

    LOG_INFO("{}[{} {}] Init MaxReuse:{} BatchCount:{}", kTag, m_name, uuid, m_maxReuseCount, m_batchCount);
}

bool AiDetector::AiSdkInit() {
    if (m_detector) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, m_name, uuid);
        return true;
    }

    constexpr int kMaxRetryBeforeDelay  = 3;
    constexpr int64_t kRetryIntervalMs  = 30 * 1000;
    constexpr int64_t kRequiredGpuMemMB = 512;

    if (m_initRetryCount >= kMaxRetryBeforeDelay) {
        auto now = util::GetMilliseconds();
        if ((now - m_lastInitFailTimeMs) < kRetryIntervalMs) {
            return false;
        }
        LOG_INFO("{}[{} {}] Init retry after {}s cooldown (retryCount:{})", kTag, m_name, uuid,
                 kRetryIntervalMs / 1000, m_initRetryCount);
    }

#ifndef COSMO_NN_USE_CPU_BACKEND
    auto availMB =
        service::ServiceRegistry::Instance().Get<service::IHardwareQuery>().GetAvailableGpuMemoryMB();
    if (availMB >= 0 && availMB < kRequiredGpuMemMB) {
        LOG_WARN("{}[{} {}] Insufficient GPU memory, skipping model load. Available:{}MB Required:{}MB", kTag,
                 m_name, uuid, availMB, kRequiredGpuMemMB);
        m_initRetryCount++;
        m_lastInitFailTimeMs = util::GetMilliseconds();
        return false;
    }
#else
    static_cast<void>(kRequiredGpuMemMB);
#endif

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet           = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
                  m_algCode, cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", kTag, m_algCode, cfgRet);
        return false;
    }
    LOG_INFO("{}cfgPath:{}, modelPath:{}", kTag, cfgPath, modelPath);
    m_detector = std::make_shared<AiDetectorUnify>(m_algCode, cfgPath, modelPath);
    auto ret   = m_detector->Init();
    if (util::ErrorEnum::Success != ret) {
        m_detector.reset();
        m_detector    = nullptr;
        action_status = ret;
        m_initRetryCount++;
        m_lastInitFailTimeMs = util::GetMilliseconds();
        LOG_WARN("{}[{} {}] {} Sdk Init Failed Get Ret {} (retryCount:{})", kTag, m_name, uuid, m_algCode,
                 ret, m_initRetryCount);
        return false;
    }
    m_initRetryCount = 0;
    action_status    = util::ErrorEnum::AI_INST_CREATED;
    m_lables         = m_detector->GetLabels();
    LOG_INFO("{}[{} {}] {} Init Sdk", kTag, m_name, uuid, m_algCode);
    m_detectorInstInit = true;
    return true;
}

void AiDetector::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) {
    AlgActionDataQueueStatus status;
    auto durationInfo = duration_stat.ComputeStats();
    status.durationInfos.push_back(durationInfo);
    if (data_queue->Status(status.queueStatus, durationSec)) {
        status.actionId = GetActionId();
        for (auto& channelNode : m_channelList) {
            status.channelIds.push_back(channelNode.channel);
            status.taskIds.insert(status.taskIds.end(), channelNode.tasks.begin(), channelNode.tasks.end());
        }
        status.actionStatus = action_status;
        queStatus.push_back(status);
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
        LOG_INFO("{}[{} {}] Stop done, thread joined", kTag, m_name, uuid);
    }
}

void AiDetector::HandFrames(std::vector<AlgDataPtr> algDatas) {
    if (0 == algDatas.size()) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!m_detector) {
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit())
            return;
    }
    std::vector<VideoFramePtr> images;
    std::vector<AlgDataPtr> activeAlgDatas;
    for (auto algData : algDatas) {
        if (algData->chanDataDec.frame->Active()) {
            images.push_back(algData->chanDataDec.frame);
            activeAlgDatas.push_back(algData);
        }
    }

    if (images.empty()) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    CheckAndActiveConfidece();
    std::vector<AiConfidence> confThres;
    std::vector<std::vector<AiDetectRstEl>> detRsts;

    if (m_activeConfidenceModifySign != m_activeConfidenceActiveSign) {
        m_activeConfidenceActiveSign = m_activeConfidenceModifySign;
        confThres                    = m_activeConfidence;
        LOG_INFO("{}[{} {}] Confidece Active. confThres size:{}", kTag, m_name, uuid, confThres.size());
    }
    action_status = m_detector->Detect(images, confThres, detRsts);
    if (util::ErrorEnum::Success != action_status) {
        LOG_ERRO("{}[{} {}] Detect Failed. Ret:{} images:{} confThres:{}", kTag, m_name, uuid, action_status,
                 images.size(), confThres.size());
        return;
    }
    if (detRsts.size() > activeAlgDatas.size()) {
        LOG_WARN("{}[{} {}] Detect result size:{} larger than active data size:{}, extra results ignored",
                 kTag, m_name, uuid, detRsts.size(), activeAlgDatas.size());
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

        algData->chanDataDetect.atomicCode  = m_algCode;
        algData->chanDataDetect.lables      = m_lables;
        algData->chanDataDetect.atomicCodes = {m_algCode};
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
AlgDataPtr AiDetector::ConfidenceFilter(AlgDataPtr dataPtr, const std::string& taskId) {
    AlgDataPtr algData = AlgDataCopy(dataPtr);
    if (!algData)
        return nullptr;

    if (!algData->chanDataDetect.detRet) {
        return dataPtr;
    }

    if (!algData->legacyDetect.atomicCodes.empty()) {
        algData->chanDataDetect.atomicCodes.insert(algData->chanDataDetect.atomicCodes.end(),
                                                   algData->legacyDetect.atomicCodes.begin(),
                                                   algData->legacyDetect.atomicCodes.end());
    }
    algData->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    if (!algData->chanDataDetect.detRet)
        return algData;
    algData->chanDataDetect.detRet->streamIndex = dataPtr->chanDataDetect.detRet->streamIndex;
    algData->chanDataDetect.detRet->frameIndex  = dataPtr->chanDataDetect.detRet->frameIndex;
    algData->chanDataDetect.detRet->timestamp   = dataPtr->chanDataDetect.detRet->timestamp;
    algData->chanDataDetect.detRet->picWidth    = dataPtr->chanDataDetect.detRet->picWidth;
    algData->chanDataDetect.detRet->picHeight   = dataPtr->chanDataDetect.detRet->picHeight;

    algData->chanDataDetect.detRet->dataType = AlgDataType::ChannelDataDetect;

    bool findTask = false;
    for (auto& taskConfidence : m_activeTaskConfidence) {
        if (taskId == taskConfidence.taskId) {
            for (auto& confidenceEl : taskConfidence.labelParams) {
                findTask = true;
                for (auto& target : dataPtr->chanDataDetect.detRet->targets) {
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
        algData->chanDataDetect.detRet->targets = dataPtr->chanDataDetect.detRet->targets;
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
        if ((data_queue->RestSize() >= m_batchCount) || ((index >= 100) && (data_queue->RestSize() > 0))) {
            size_t detnum     = data_queue->RestSize();
            auto inputFpsCalc = input_fps_calc.FpsWithFrame();
            handle_frame_cnt  = inputFpsCalc.first;
            in_fps            = inputFpsCalc.second;

            std::vector<AlgDataPtr> algDatas;
            for (size_t i = 0; i < detnum && i < m_batchCount; i++) {
                auto data          = data_queue->Pop();
                AlgDataPtr algData = AlgDataCopy(data);
                if (algData) {
                    algDatas.push_back(algData);
                }
            }
            if (!algDatas.empty()) {
                if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetActionSwitch(
                        GetActionId())) {
                    duration_stat.BeginSample();
                    HandFrames(algDatas);
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
    if (m_detector) {
        LOG_INFO("{}[{} {}] Thread exiting, releasing YOLO model to free VRAM", kTag, m_name, uuid);
        m_detector.reset();
        m_detector         = nullptr;
        m_detectorInstInit = false;
    }
    LOG_INFO("[{} {}] THREAD [{}] Stop ", m_name, uuid, Name());
}

// ChannelExist, TaskExist, TaskIsFull, TaskIsEmpty, ChannelCount, TaskCount,
// AddTask, RemoveTask — moved to AiDetectorTask.cc

}  // namespace cosmo
