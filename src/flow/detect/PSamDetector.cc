// PSamDetector.cc — SAM2 segmentation model implementation for picture/snapshot mode.

#include "flow/detect/PSamDetector.h"

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Log.h"

namespace cosmo {

PSamDetector::PSamDetector(ActionNode& action, const std::string& taskId) : PActionBase(action, taskId) {
    input_type_ = Sam2InputType::BOX;
    for (auto& param : action.configObject.params) {
        AnalysisKey(param);
    }
    LOG_INFO("[{} {}] PSamDetector Init InputType:{}", GetTaskId(), GetFlowActionId(),
             (input_type_ == Sam2InputType::BOX) ? "BOX" : "POINT");
}

PSamDetector::~PSamDetector() {
    LOG_INFO("[{} {}] PSamDetector Stop & Delete", GetTaskId(), GetFlowActionId());
    PSamDetector::ActionDestroy();
}

bool PSamDetector::ActionInit() {
    if (inst_)
        return true;

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet           = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
                  GetAtomicCode(), cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("Get Model Configure Failed. AlgCode:{} code:{}", GetAtomicCode(), cfgRet);
        return false;
    }

    inst_    = std::make_shared<Sam2SegmenterUnify>(GetAtomicCode(), cfgPath, modelPath);
    auto ret = inst_->Init();
    if (util::ErrorEnum::Success != ret) {
        inst_.reset();
        inst_ = nullptr;
        LOG_WARN("[{} {}] SAM2 Sdk Init Failed Get Ret {}", GetTaskId(), GetFlowActionId(), ret);
        return false;
    }

    LOG_INFO("[{} {}] Init SAM2 Sdk Success", GetTaskId(), GetFlowActionId());
    return true;
}

void PSamDetector::ActionDestroy() {
    if (inst_) {
        inst_.reset();
        inst_ = nullptr;
    }
}

bool PSamDetector::ValidKey(const MsgDynamicKeyValue& param) const {
    return !param.keys.empty() && param.keys[0] == "inputType";
}

bool PSamDetector::AnalysisKey(const MsgDynamicKeyValue& param) {
    if (!ValidKey(param))
        return false;

    if (param.keys[0] == "inputType") {
        if (param.value == "point") {
            input_type_ = Sam2InputType::POINT;
        } else {
            input_type_ = Sam2InputType::BOX;
        }
        LOG_INFO("[{} {}] Set param inputType to {}", GetTaskId(), GetFlowActionId(), param.value);
        return true;
    }
    return false;
}

bool PSamDetector::ModifyParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

bool PSamDetector::SetParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    input_type_ = Sam2InputType::BOX;
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

util::ErrorEnum PSamDetector::HandPic(AlgDataPtr algData) {
    if (!inst_) {
        return util::ErrorEnum::NotInit;
    }
    if (!algData || !algData->chanDataDec.frame) {
        return util::ErrorEnum::InvalidParam;
    }

    Sam2InputType currentInputType;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        currentInputType = input_type_;
    }

    std::vector<Sam2PromptInput> prompts;
    auto origImg = algData->chanDataDec.frame;
    float imgW   = origImg->GetWidth();
    float imgH   = origImg->GetHeight();

    // If a prior action (e.g. YOLO or DINO) produced detection boxes, use them as prompts
    bool hasPreviousTargets =
        algData->chanDataDetect.detRet && !algData->chanDataDetect.detRet->targets.empty();

    if (hasPreviousTargets) {
        for (const auto& tgt : algData->chanDataDetect.detRet->targets) {
            Sam2PromptInput prompt;
            prompt.input_type = currentInputType;
            if (currentInputType == Sam2InputType::BOX) {
                prompt.coords = {static_cast<float>(tgt.box.x), static_cast<float>(tgt.box.y),
                                 static_cast<float>(tgt.box.x + tgt.box.width),
                                 static_cast<float>(tgt.box.y + tgt.box.height)};
            } else {
                // For point mode, use the center of the detection box
                prompt.coords = {static_cast<float>(tgt.box.x + tgt.box.width / 2.0f),
                                 static_cast<float>(tgt.box.y + tgt.box.height / 2.0f)};
                prompt.labels = {1.0};  // 1 = foreground point
            }
            prompts.push_back(prompt);
        }
    } else {
        // No prior detection boxes: segment the entire image by default
        Sam2PromptInput prompt;
        prompt.input_type = currentInputType;
        if (currentInputType == Sam2InputType::BOX) {
            // Use the full image as the bounding box
            prompt.coords = {0.0f, 0.0f, imgW, imgH};
        } else {
            // Use the image center as a point prompt
            prompt.coords = {imgW / 2.0f, imgH / 2.0f};
            prompt.labels = {1.0};
        }
        prompts.push_back(prompt);
    }

    // SAM2 SDK requires images.size() == prompts.size() (1:1 mapping)
    // Each prompt needs a corresponding image, so duplicate the same image N times
    std::vector<VideoFramePtr> images;
    for (size_t i = 0; i < prompts.size(); ++i) {
        images.push_back(algData->chanDataDec.frame);
    }

    std::vector<AiDetectRstEl> samResults;
    auto ret = inst_->Segment(images, prompts, samResults);

    if (ret != util::ErrorEnum::Success) {
        LOG_WARN("[{} {}] SAM2 Segment failed: {}", GetTaskId(), GetFlowActionId(), ret);
        return ret;
    }

    if (!algData->chanDataDetect.detRet) {
        algData->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    }

    if (!hasPreviousTargets) {
        // No prior targets: use segmentation masks/polygons as new targets
        algData->chanDataDetect.detRet->targets = samResults;
    } else {
        // Prior targets exist: align and update segmentation polygons by inference order
        for (size_t i = 0; i < samResults.size() && i < algData->chanDataDetect.detRet->targets.size(); ++i) {
            algData->chanDataDetect.detRet->targets[i].mask = samResults[i].mask;
        }
    }

    return util::ErrorEnum::Success;
}

}  // namespace cosmo
