// PDinoDetector.cc — Grounding DINO detector implementation for picture/snapshot mode.

#include "flow/detect/PDinoDetector.h"

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

namespace cosmo {

PDinoDetector::PDinoDetector(ActionNode& action, const std::string& taskId)
    : PActionBase(action, taskId), prompt_("detect objects") {
    box_confidence_  = 0.25f;
    text_confidence_ = 0.3f;

    for (auto& param : action.configObject.params) {
        AnalysisKey(param);
    }
    LOG_INFO("[{} {}] PDinoDetector Init Prompt:{}", GetTaskId(), GetFlowActionId(), prompt_);
}

PDinoDetector::~PDinoDetector() {
    LOG_INFO("[{} {}] PDinoDetector Stop & Delete", GetTaskId(), GetFlowActionId());
    PDinoDetector::ActionDestroy();
}

bool PDinoDetector::ActionInit() {
    if (inst_) {
        return true;
    }

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet           = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
                  GetAtomicCode(), cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("Get Model Configure Failed. AlgCode:{} code:{}", GetAtomicCode(), cfgRet);
        return false;
    }

    std::string modelDir  = modelPath.substr(0, modelPath.find_last_of("/\\"));
    std::string vocabPath = modelDir + "/vocab.txt";
    if (vocabPath.empty()) {
        LOG_WARN("Warn: vocab file not found for Dino: {}", GetAtomicCode());
    }

    inst_    = std::make_shared<DinoDetectorUnify>(GetAtomicCode(), cfgPath, modelPath, vocabPath);
    auto ret = inst_->Init();
    if (util::ErrorEnum::Success != ret) {
        inst_.reset();
        inst_ = nullptr;
        LOG_WARN("[{} {}] {} DINO Sdk Init Failed Get Ret {}", GetTaskId(), GetFlowActionId(),
                 GetAtomicCode(), ret);
        return false;
    }

    LOG_INFO("[{} {}] Init DINO Sdk Success", GetTaskId(), GetFlowActionId());
    return true;
}

void PDinoDetector::ActionDestroy() {
    if (inst_) {
        inst_.reset();
        inst_ = nullptr;
    }
}

bool PDinoDetector::ValidKey(const MsgDynamicKeyValue& param) const {
    if (param.keys.empty())
        return false;
    if (param.keys[0] != key::AI_PARAM && param.keys[0] != "keywords") {
        return false;
    }
    return true;
}

bool PDinoDetector::AnalysisKey(const MsgDynamicKeyValue& param) {
    if (!ValidKey(param)) {
        return false;
    }

    if (param.keys[0] == "keywords") {
        prompt_ = param.value;
        LOG_INFO("[{} {}] Set param.key keywords value {}", GetTaskId(), GetFlowActionId(), prompt_);
        return true;
    } else if (param.keys.size() >= 3 && param.keys[0] == key::AI_PARAM) {
        if (param.keys[1] == "box" && param.keys[2] == key::CONFIDENCE) {
            box_confidence_ = util::ParseFloat(param.value);
            LOG_INFO("[{} {}] Set param.key box.confidence value {}", GetTaskId(), GetFlowActionId(),
                     box_confidence_);
            return true;
        } else if (param.keys[1] == "text" && param.keys[2] == key::CONFIDENCE) {
            text_confidence_ = util::ParseFloat(param.value);
            LOG_INFO("[{} {}] Set param.key text.confidence value {}", GetTaskId(), GetFlowActionId(),
                     text_confidence_);
            return true;
        }
    }
    return false;
}

bool PDinoDetector::ModifyParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

bool PDinoDetector::SetParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    // Reset to default parameters
    box_confidence_  = 0.25f;
    text_confidence_ = 0.3f;

    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

util::ErrorEnum PDinoDetector::HandPic(AlgDataPtr algData) {
    if (!inst_) {
        return util::ErrorEnum::NotInit;
    }
    if (!algData || !algData->chanDataDec.frame) {
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<VideoFramePtr> images;
    images.push_back(algData->chanDataDec.frame);

    std::string currentPrompt;
    float currentBoxConf;

    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        currentPrompt  = prompt_;
        currentBoxConf = box_confidence_;
    }

    std::vector<std::vector<AiDetectRstEl>> detRsts;
    auto ret = inst_->Detect(images, currentPrompt, detRsts);

    if (ret != util::ErrorEnum::Success) {
        LOG_WARN("[{} {}] DINO Detect failed: {}", GetTaskId(), GetFlowActionId(), ret);
        return ret;
    }

    if (!algData->chanDataDetect.detRet) {
        algData->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    }

    // Filter by boxConfidence and format output
    if (!detRsts.empty()) {
        for (auto& rst : detRsts[0]) {
            if (rst.confidence.confidence >= currentBoxConf) {
                // Use detection label as-is (backend may return index or actual name)
                algData->chanDataDetect.detRet->targets.push_back(rst);
            }
        }
    }

    return util::ErrorEnum::Success;
}

}  // namespace cosmo
