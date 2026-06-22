// Picture-detection task base — manages action orchestration for single-image analysis.

#pragma once

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
    PActionBasePtr sonAction;   // Child node instance
};

struct PTaskElement {
    bool is_started{false};
    int startFailedCount{0};
    std::shared_mutex mtx;           // Lock for task execution
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

    // Apply task parameters to algorithm action instances
    bool ModifyTaskParam(PTaskElementPtr task, MsgTaskConfig& param);

private:
    void UploadImage(std::vector<uint8_t>& data, const std::string& url, const std::string& sign);
    void DetTargetHandFullPicture(AlgDataPtr algData, const std::vector<MsgTaskArea>& inAreas,
                                  MsgPTaskDetectPicRecv& data, MsgPTaskDetectPicSend& retData);
    std::shared_mutex m_mtx;

    PDetectorMng m_detectorMng;              // Detector management instance
    PClassifierMng m_classifierMng;          // Classifier management instance
    PLandmarkMng m_landmarkMng;              // Landmark management instance
    PRecognizerMng m_recognizerMng;          // Feature extractor management instance
    PLogicalJudgmentMng m_logicJudgmentMng;  // Logical judgment management instance
};

using PTaskBasePtr = std::shared_ptr<PTaskBase>;
}  // namespace cosmo
