// PDinoDetector.cc — Grounding DINO detector implementation for picture/snapshot mode.

#include "flow/detect/PDinoDetector.h"

#include <cmath>
#include <utility>

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

namespace cosmo {

namespace {

    bool ParsePictureDinoConfidence(const std::string& text, float& value) {
        try {
            std::size_t consumed = 0;
            const float parsed   = std::stof(text, &consumed);
            if (consumed != text.size() || !std::isfinite(parsed) || parsed < 0.0f || parsed > 1.0f)
                return false;
            value = parsed;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool NormalizePictureDinoPrompt(const std::string& text, std::string& prompt) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return false;
        const auto last = text.find_last_not_of(" \t\r\n");
        prompt          = text.substr(first, last - first + 1);
        return !prompt.empty();
    }

}  // namespace

PDinoDetector::PDinoDetector(ActionNode& action, const std::string& taskId)
    : PActionBase(action, taskId), prompt_("person") {
    box_confidence_  = 0.3f;
    text_confidence_ = 0.25f;

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
    auto cfgRet = service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
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
        std::string prompt;
        if (!NormalizePictureDinoPrompt(param.value.ToString(), prompt)) {
            LOG_WARN("[{} {}] DINO prompt is empty", GetTaskId(), GetFlowActionId());
            return false;
        }
        prompt_ = std::move(prompt);
        LOG_INFO("[{} {}] Set param.key keywords value {}", GetTaskId(), GetFlowActionId(), prompt_);
        return true;
    } else if (param.keys.size() >= 3 && param.keys[0] == key::AI_PARAM) {
        if (param.keys[1] == "box" && param.keys[2] == key::CONFIDENCE) {
            if (!ParsePictureDinoConfidence(param.value.ToString(), box_confidence_)) {
                LOG_WARN("[{} {}] Invalid DINO box confidence: {}", GetTaskId(), GetFlowActionId(),
                         param.value.ToString());
                return false;
            }
            LOG_INFO("[{} {}] Set param.key box.confidence value {}", GetTaskId(), GetFlowActionId(),
                     box_confidence_);
            return true;
        } else if (param.keys[1] == "text" && param.keys[2] == key::CONFIDENCE) {
            if (!ParsePictureDinoConfidence(param.value.ToString(), text_confidence_)) {
                LOG_WARN("[{} {}] Invalid DINO text confidence: {}", GetTaskId(), GetFlowActionId(),
                         param.value.ToString());
                return false;
            }
            LOG_INFO("[{} {}] Set param.key text.confidence value {}", GetTaskId(), GetFlowActionId(),
                     text_confidence_);
            return true;
        }
    }
    return false;
}

bool PDinoDetector::ModifyParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    const auto original_prompt = prompt_;
    const float original_box   = box_confidence_;
    const float original_text  = text_confidence_;
    bool valid                 = true;
    for (auto& param : params) {
        valid = AnalysisKey(param) && valid;
    }
    if (!valid) {
        prompt_          = original_prompt;
        box_confidence_  = original_box;
        text_confidence_ = original_text;
    }
    return valid;
}

bool PDinoDetector::SetParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    const auto original_prompt = prompt_;
    const float original_box   = box_confidence_;
    const float original_text  = text_confidence_;
    // Reset to default parameters
    box_confidence_  = 0.3f;
    text_confidence_ = 0.25f;

    bool valid = true;
    for (auto& param : params) {
        valid = AnalysisKey(param) && valid;
    }
    if (!valid) {
        prompt_          = original_prompt;
        box_confidence_  = original_box;
        text_confidence_ = original_text;
    }
    return valid;
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
    float currentTextConf;

    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        currentPrompt   = prompt_;
        currentBoxConf  = box_confidence_;
        currentTextConf = text_confidence_;
    }

    std::vector<std::vector<AiDetectRstEl>> detRsts;
    DinoDetectionOptions options;
    options.box_threshold  = currentBoxConf;
    options.text_threshold = currentTextConf;
    auto ret               = inst_->Detect(images, currentPrompt, options, detRsts);

    if (ret != util::ErrorEnum::Success) {
        LOG_WARN("[{} {}] DINO Detect failed: {}", GetTaskId(), GetFlowActionId(), ret);
        return ret;
    }

    if (!algData->chanDataDetect.detRet) {
        algData->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    }

    if (!detRsts.empty()) {
        for (auto& rst : detRsts[0]) {
            algData->chanDataDetect.detRet->targets.push_back(rst);
        }
    }

    return util::ErrorEnum::Success;
}

}  // namespace cosmo
