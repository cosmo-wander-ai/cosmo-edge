// Picture-detection task base — manages action orchestration for single-image analysis.

#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "flow/action/ActionParam.h"
#include "flow/action/AlgActionBase.h"
#include "flow/action/PActionBase.h"
#include "flow/classify/PClassifierMng.h"
#include "flow/detect/PDetectorMng.h"
#include "flow/landmark/PLandmarkMng.h"
#include "flow/logical/PLogicalJudgmentMng.h"
#include "flow/recognizer/PRecognizerMng.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {
struct PTaskAction {
    ActionNode action;          // Orchestration parameters
    PActionBasePtr actionInst;  // Current action instance
    std::vector<MsgDynamicKeyValue> modelParams;
    PActionBasePtr sonAction;  // Child node instance
};

struct PTaskElement {
    std::atomic<bool> is_started{false};
    int startFailedCount{0};
    std::mutex mtx;
    std::condition_variable idle;
    bool executing{false};
    bool retired{false};             // Lock for task execution
    std::string taskId;              // Globally unique task ID
    std::string flowActionId{"-1"};  // Root action's flowActionId
    std::string errorInfo;
    std::vector<PTaskAction> actions;  // Algorithm orchestration instances
    MsgTaskConfig params;              // Algorithm parameters
    ActionAlgPtr action_alg;           // Algorithm orchestration config

    // Convenience getter (from action_alg)
    std::string GetAlgName() const {
        return action_alg ? action_alg->algorithmName : "";
    }
    std::string GetAlgId() const {
        return action_alg ? action_alg->algorithmCode : "";
    }
    std::string GetVersion() const {
        return action_alg ? action_alg->algorithmUpdateTime : "";
    }
};
using PTaskElementPtr = std::shared_ptr<PTaskElement>;

// A task lease serializes mutable model instances without holding a lock during inference.
class PictureTaskLease {
public:
    explicit PictureTaskLease(PTaskElementPtr task) : task_(std::move(task)) {
        std::unique_lock<std::mutex> lock(task_->mtx);
        task_->idle.wait(lock, [&] { return !task_->executing || task_->retired; });
        active_ = !task_->retired;
        if (active_)
            task_->executing = true;
    }
    ~PictureTaskLease() {
        if (!active_)
            return;
        {
            std::lock_guard<std::mutex> lock(task_->mtx);
            task_->executing = false;
        }
        task_->idle.notify_all();
    }
    explicit operator bool() const {
        return active_;
    }
    PictureTaskLease(const PictureTaskLease&)            = delete;
    PictureTaskLease& operator=(const PictureTaskLease&) = delete;

private:
    PTaskElementPtr task_;
    bool active_{false};
};

class PTaskBase {
public:
    PTaskBase();
    ~PTaskBase();

    PTaskElementPtr TaskCreate(const std::string& taskId, ActionAlgPtr actionAlg);
    bool TaskDelete(PTaskElementPtr task);

    bool TaskActionInit(PTaskElementPtr task);
    bool TaskActionDestroy(PTaskElementPtr task);

    util::ErrorEnum TaskDetectPic(PTaskElementPtr task, MsgPTaskDetectPicRecv& data,
                                  MsgPTaskDetectPicSend& retData);

    // Execute an already decoded frame. Shared by the HTTP path and deterministic workflow tests.
    util::ErrorEnum ExecutePicture(PTaskElementPtr task, AlgDataPtr input,
                                   const MsgPTaskDetectPicRecv& request, MsgPTaskDetectPicSend& response,
                                   AlgDataPtr& rendered, AlgDataPtr reference = nullptr,
                                   AlgDataPtr* referenceRendered = nullptr);

    // Apply task parameters to algorithm action instances
    bool ModifyTaskParam(PTaskElementPtr task, MsgTaskConfig& param);

private:
    util::ErrorEnum ExecutePictureImpl(PTaskElementPtr task, AlgDataPtr input,
                                       const MsgPTaskDetectPicRecv& request, MsgPTaskDetectPicSend& response,
                                       AlgDataPtr& rendered, AlgDataPtr reference,
                                       AlgDataPtr* referenceRendered, bool referencePass);
    void UploadImage(std::vector<uint8_t>& data, const std::string& url, const std::string& sign);
    void DetTargetHandFullPicture(AlgDataPtr algData, MsgPTaskDetectPicRecv& data,
                                  MsgPTaskDetectPicSend& retData);
    std::shared_mutex m_mtx;

    PDetectorMng m_detectorMng;              // Detector management instance
    PClassifierMng m_classifierMng;          // Classifier management instance
    PLandmarkMng m_landmarkMng;              // Landmark management instance
    PRecognizerMng m_recognizerMng;          // Feature extractor management instance
    PLogicalJudgmentMng m_logicJudgmentMng;  // Logical judgment management instance
};

using PTaskBasePtr = std::shared_ptr<PTaskBase>;
}  // namespace cosmo
