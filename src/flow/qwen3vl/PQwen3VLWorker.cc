// PQwen3VLWorker — Qwen3VL vision-language model wrapper for single-image analysis mode.

#include "flow/qwen3vl/PQwen3VLWorker.h"

#include <algorithm>

#include "flow/common/LlmYesNoJudge.h"
#include "flow/qwen3vl/OpenAiVlmClient.h"
#include "service/ai/ILlmInferService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/Log.h"
#include "util/SafeParse.h"

namespace cosmo {

PQwen3VLWorker::PQwen3VLWorker(ActionNode& action, const std::string& task_id)
    : PActionBase(action, task_id), prompt_(""), gen_param_({}) {
    advanced_mode_    = false;
    generation_style_ = Qwen3VLGenerationStyle::STANDARD;
    for (auto& param : action.configObject.params) {
        AnalysisKey(param);
    }
    ApplyGenerationStyle();
    LOG_INFO("[{} {}] PQwen3VLWorker Init Prompt:{}", GetTaskId(), GetFlowActionId(), prompt_);
}

PQwen3VLWorker::~PQwen3VLWorker() {
    LOG_INFO("[{} {}] PQwen3VLWorker Stop & Delete", GetTaskId(), GetFlowActionId());
    PQwen3VLWorker::ActionDestroy();
}

bool PQwen3VLWorker::ActionInit() {
    if (worker_registered_)
        return true;
    if (open_ai_config_.Enabled()) {
        LOG_INFO("[{} {}] PQwen3VLWorker using OpenAI VLM provider", GetTaskId(), GetFlowActionId());
        return true;
    }

    // Ensure shared LLM instance is loaded
    if (!service::ServiceRegistry::Instance().Get<service::ILlmInferService>().EnsureInit(GetAtomicCode())) {
        LOG_WARN("[{} {}] LlmInferService Init Failed", GetTaskId(), GetFlowActionId());
        return false;
    }
    // Participate in the reference-counted lifecycle so that the shared model is
    // only released when ALL users (video workers + single-image workers) are done.
    service::ServiceRegistry::Instance().Get<service::ILlmInferService>().NotifyWorkerStart();
    worker_registered_ = true;
    LOG_INFO("[{} {}] Init Qwen3VL Shared Sdk Success", GetTaskId(), GetFlowActionId());
    return true;
}

void PQwen3VLWorker::ActionDestroy() {
    // Decrement the active worker count only if we successfully registered.
    // Without this guard, a failed ActionInit() (which never called
    // NotifyWorkerStart) followed by destruction would drive the count negative.
    if (worker_registered_) {
        worker_registered_ = false;
        service::ServiceRegistry::Instance().Get<service::ILlmInferService>().NotifyWorkerStop();
    }
}

bool PQwen3VLWorker::ValidKey(MsgDynamicKeyValue& param) {
    return !param.keys.empty() &&
           (param.keys[0] == "keywords" || param.keys[0] == "advanced_mode" ||
            param.keys[0] == "generationStyle" || param.keys[0] == "doSample" || param.keys[0] == "topK" ||
            param.keys[0] == "topP" || param.keys[0] == "temperature" || param.keys[0] == "vlmProvider" ||
            param.keys[0] == "provider" || param.keys[0] == "base_url" || param.keys[0] == "api_key" ||
            param.keys[0] == "model" || param.keys[0] == "endpoint" || param.keys[0] == "timeout_ms" ||
            param.keys[0] == "max_tokens" || param.keys[0] == "openai");
}

bool PQwen3VLWorker::AnalysisKey(MsgDynamicKeyValue& param) {
    if (param.key == "inputType") {
        target_input_ = param.value == "targets";
        return true;
    }
    if (!ValidKey(param))
        return false;

    if (param.keys[0] == "advanced_mode") {
        advanced_mode_ = (param.value == "1" || param.value == "true");
        LOG_INFO("[{} {}] param advanced_mode {}", GetTaskId(), GetFlowActionId(), advanced_mode_);
        return true;
    } else if (param.keys[0] == "keywords") {
        prompt_ = param.value;
        LOG_INFO("[{} {}] param keywords {}", GetTaskId(), GetFlowActionId(), prompt_);
        return true;
    } else if (param.keys[0] == "generationStyle") {
        if (param.value == "strict")
            generation_style_ = Qwen3VLGenerationStyle::RIGOROUS;
        else if (param.value == "standard")
            generation_style_ = Qwen3VLGenerationStyle::STANDARD;
        else if (param.value == "creative")
            generation_style_ = Qwen3VLGenerationStyle::DIVERGENT;
        else if (param.value == "custom")
            generation_style_ = Qwen3VLGenerationStyle::CUSTOM;
        LOG_INFO("[{} {}] param generationStyle {}", GetTaskId(), GetFlowActionId(), param.value);
        return true;
    } else if (param.keys[0] == "doSample") {
        gen_param_.do_sample = (param.value == "1" || param.value == "true");
        return true;
    } else if (param.keys[0] == "topK") {
        gen_param_.top_k = static_cast<int32_t>(util::ParseInt(param.value));
        return true;
    } else if (param.keys[0] == "topP") {
        gen_param_.top_p = util::ParseFloat(param.value);
        return true;
    } else if (param.keys[0] == "temperature") {
        gen_param_.temperature = util::ParseFloat(param.value);
        return true;
    } else if (param.keys[0] == "vlmProvider" || param.keys[0] == "provider") {
        open_ai_config_.provider = param.value;
        return true;
    } else if (param.keys[0] == "base_url" ||
               (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "base_url")) {
        open_ai_config_.base_url = param.value;
        return true;
    } else if (param.keys[0] == "api_key" ||
               (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "api_key")) {
        open_ai_config_.api_key = param.value;
        return true;
    } else if (param.keys[0] == "model" ||
               (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "model")) {
        open_ai_config_.model = param.value;
        return true;
    } else if (param.keys[0] == "endpoint" ||
               (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "endpoint")) {
        open_ai_config_.endpoint = param.value;
        return true;
    } else if (param.keys[0] == "timeout_ms" ||
               (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "timeout_ms")) {
        open_ai_config_.timeout_ms = util::ParseInt(param.value, open_ai_config_.timeout_ms);
        return true;
    } else if (param.keys[0] == "max_tokens" ||
               (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "max_tokens")) {
        open_ai_config_.max_tokens = util::ParseInt(param.value, open_ai_config_.max_tokens);
        return true;
    } else if (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "temperature") {
        open_ai_config_.temperature = util::ParseFloat(param.value, open_ai_config_.temperature);
        return true;
    } else if (param.keys.size() >= 2 && param.keys[0] == "openai" && param.keys[1] == "top_p") {
        open_ai_config_.top_p = util::ParseFloat(param.value, open_ai_config_.top_p);
        return true;
    }

    return false;
}

void PQwen3VLWorker::ApplyGenerationStyle() {
    switch (generation_style_) {
        case Qwen3VLGenerationStyle::RIGOROUS:
            gen_param_.do_sample   = false;
            gen_param_.top_k       = 1;
            gen_param_.top_p       = 0.0f;
            gen_param_.temperature = 0.0f;
            break;
        case Qwen3VLGenerationStyle::STANDARD:
            gen_param_.do_sample   = true;
            gen_param_.top_k       = 20;
            gen_param_.top_p       = 0.5f;
            gen_param_.temperature = 0.5f;
            break;
        case Qwen3VLGenerationStyle::DIVERGENT:
            gen_param_.do_sample   = true;
            gen_param_.top_k       = 50;
            gen_param_.top_p       = 0.8f;
            gen_param_.temperature = 0.8f;
            break;
        case Qwen3VLGenerationStyle::CUSTOM:
            // Custom mode relies on externally-provided values; do not override
            break;
    }
}

bool PQwen3VLWorker::ModifyParam(const std::string& /*task_id*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    ApplyGenerationStyle();
    return true;
}

bool PQwen3VLWorker::SetParam(const std::string& /*task_id*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    // Reset to default parameters
    prompt_           = "";
    target_input_     = false;
    advanced_mode_    = false;
    generation_style_ = Qwen3VLGenerationStyle::STANDARD;
    gen_param_        = {};
    open_ai_config_   = {};

    for (auto& param : params) {
        AnalysisKey(param);
    }
    ApplyGenerationStyle();
    return true;
}

util::ErrorEnum PQwen3VLWorker::HandPic(AlgDataPtr alg_data) {
    if (!alg_data || !alg_data->chanDataDec.frame) {
        return util::ErrorEnum::InvalidParam;
    }

    OpenAiVlmConfig open_ai_config;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        open_ai_config = open_ai_config_;
    }

    if (!open_ai_config.Enabled() &&
        !service::ServiceRegistry::Instance().Get<service::ILlmInferService>().IsInitialized()) {
        if (!service::ServiceRegistry::Instance().Get<service::ILlmInferService>().EnsureInit(
                GetAtomicCode())) {
            return util::ErrorEnum::NotInit;
        }
    }

    std::string prompt;
    Qwen3VLGenerationParam gen_param;

    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        gen_param = gen_param_;
        if (advanced_mode_) {
            prompt = prompt_ + "，回答是或者否,不要换行，不要其他内容。";
        } else {
            prompt = "判断图片中是否存在【" + prompt_ + "】目标，回答是或者否,不要换行，不要其他内容。";
        }
    }

    bool targets_input;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        targets_input = target_input_;
    }
    if (!alg_data->chanDataDetect.detRet) {
        if (targets_input)
            return util::ErrorEnum::FlowDataInvalid;
        alg_data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    }
    auto& targets = alg_data->chanDataDetect.detRet->targets;
    if (!targets_input) {
        targets.clear();
        AiDetectRstEl target;
        target.targetId = GetFlowActionId() + ":0";
        targets.push_back(target);
    }
    auto& transform = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
    for (auto& target : targets) {
        if (target.bFilter)
            continue;
        auto frame = alg_data->chanDataDec.frame;
        if (targets_input) {
            const int x = std::max(0, target.box.x), y = std::max(0, target.box.y);
            const int right = std::min(static_cast<int>(frame->GetWidth()), target.box.x + target.box.width);
            const int bottom =
                std::min(static_cast<int>(frame->GetHeight()), target.box.y + target.box.height);
            if (right <= x || bottom <= y)
                return util::ErrorEnum::InvalidParam;
            frame = transform.Crop(frame, util::Box(x, y, right - x, bottom - y));
        }
        if (!frame || !transform.EnsureHostData(frame))
            return util::ErrorEnum::InvalidParam;
        std::vector<Qwen3VLResult> results;
        const auto status =
            open_ai_config.Enabled()
                ? OpenAiVlmClient::Generate(open_ai_config, {frame}, {prompt}, gen_param, results)
                : service::ServiceRegistry::Instance().Get<service::ILlmInferService>().Generate(
                      {frame}, {prompt}, gen_param, results);
        if (status != util::ErrorEnum::Success)
            return status;
        if (results.size() != 1)
            return util::ErrorEnum::FlowDataInvalid;
        const auto judge           = ParseJudgeYesNo(results.front().text);
        const std::string decision = judge == LlmJudgeYesNo::Unknown
                                         ? "unknown"
                                         : (judge == LlmJudgeYesNo::Yes ? "matched" : "not_matched");
        target.bLogicResult        = judge == LlmJudgeYesNo::Yes;
        AiConfidence confidence;
        confidence.label       = results.front().text;
        confidence.atomic_code = GetAtomicCode();
        confidence.confidence  = 1.0F;
        target.classifyRst.push_back(confidence);
        alg_data->pictureRules[GetFlowActionId()][target.targetId] = decision;
        alg_data->pictureDecisions[target.targetId]                = decision;
        if (!targets_input) {
            alg_data->pictureDecision                          = decision;
            alg_data->pictureRules[GetFlowActionId()]["image"] = decision;
        }
    }
    alg_data->bHaveLogic = true;

    return util::ErrorEnum::Success;
}

}  // namespace cosmo
