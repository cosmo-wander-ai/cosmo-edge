// Qwen3VLWorkerParam.cc — Parameter and style management for Qwen3VLWorker.
// Split from Qwen3VLWorker.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "flow/common/AlgDataUnit.h"
#include "flow/common/FlowTaskUtil.h"
#include "flow/common/LlmYesNoJudge.h"
#include "flow/qwen3vl/OpenAiVlmClient.h"
#include "flow/qwen3vl/Qwen3VLWorker.h"
#include "media/VideoFrame.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag = "QWEN3VL ";
namespace cosmo {
namespace {

    Qwen3VLWorkerParamEl& EnsureTaskParam(std::vector<Qwen3VLWorkerParamEl>& params,
                                          const std::string& task_id) {
        auto it = std::find_if(params.begin(), params.end(),
                               [&task_id](const Qwen3VLWorkerParamEl& el) { return el.task_id == task_id; });
        if (it == params.end()) {
            Qwen3VLWorkerParamEl el;
            el.task_id = task_id;
            params.push_back(el);
            return params.back();
        }
        return *it;
    }

}  // namespace

void Qwen3VLWorker::ApplyGenerationStyle(Qwen3VLWorkerParamEl& param) {
    switch (param.generation_style) {
        case Qwen3VLGenerationStyle::RIGOROUS:
            param.gen_param.temperature = 0.0f;
            param.gen_param.top_p       = 0.0f;
            param.gen_param.top_k       = 1;
            param.gen_param.do_sample   = false;
            break;
        case Qwen3VLGenerationStyle::STANDARD:
            param.gen_param.temperature = 0.7f;
            param.gen_param.top_p       = 0.8f;
            param.gen_param.top_k       = 20;
            param.gen_param.do_sample   = true;
            break;
        case Qwen3VLGenerationStyle::DIVERGENT:
            param.gen_param.temperature = 0.9f;
            param.gen_param.top_p       = 0.9f;
            param.gen_param.top_k       = 40;
            param.gen_param.do_sample   = true;
            break;
        case Qwen3VLGenerationStyle::CUSTOM:
        default:
            break;
    }
}

namespace {

    bool IsTrueValue(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes" || value == "on";
    }

    bool SetOpenAiConfigValue(Qwen3VLWorkerParamEl& param, const std::string& key, const std::string& value) {
        if (key == "vlmProvider" || key == "provider") {
            param.open_ai_config.provider = value;
            return true;
        }
        if (key == "openai.provider") {
            param.open_ai_config.provider = value;
            return true;
        }
        if (key == "openai.base_url" || key == "base_url") {
            param.open_ai_config.base_url = value;
            return true;
        }
        if (key == "openai.api_key" || key == "api_key") {
            param.open_ai_config.api_key = value;
            return true;
        }
        if (key == "openai.model" || key == "model") {
            param.open_ai_config.model = value;
            return true;
        }
        if (key == "openai.endpoint" || key == "endpoint") {
            param.open_ai_config.endpoint = value;
            return true;
        }
        if (key == "openai.timeout_ms" || key == "timeout_ms") {
            param.open_ai_config.timeout_ms = util::ParseInt(value, param.open_ai_config.timeout_ms);
            return true;
        }
        if (key == "openai.max_tokens" || key == "max_tokens") {
            param.open_ai_config.max_tokens = util::ParseInt(value, param.open_ai_config.max_tokens);
            return true;
        }
        if (key == "openai.temperature") {
            param.open_ai_config.temperature = util::ParseFloat(value, param.open_ai_config.temperature);
            return true;
        }
        if (key == "openai.top_p") {
            param.open_ai_config.top_p = util::ParseFloat(value, param.open_ai_config.top_p);
            return true;
        }
        return false;
    }

}  // namespace

bool Qwen3VLWorker::ValidKey(MsgDynamicKeyValue& param) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            alg_code_, uuid);
        return false;
    }
    return true;
}

bool Qwen3VLWorker::AnalysisKey(const std::string& /*channel_id*/, const std::string& tid,
                                MsgDynamicKeyValue& param) {
    std::string key_str = param.key.ToString();
    bool is_prompt      = (key_str == "keywords") || (key_str == "prompt") ||
                     (param.keys.size() >= 1 && param.keys[0] == "keywords") ||
                     (param.keys.size() >= 2 && param.keys[1] == "prompt");
    if (is_prompt) {
        std::string value  = param.value.ToString();
        auto& local_param  = EnsureTaskParam(params_.param, tid);
        local_param.prompt = value;
        LOG_INFO("ModifyParam [{} {}] Task:{} set prompt: \"{}\"", alg_code_, uuid, tid, value);
        return true;
    }
    if (key_str == "advanced_mode") {
        std::string value_str     = param.value.ToString();
        auto& local_param         = EnsureTaskParam(params_.param, tid);
        local_param.advanced_mode = IsTrueValue(value_str);
        LOG_INFO("[MODIFYPARAM] [{} {}] Task:{} set advanced_mode: {}", alg_code_, uuid, tid,
                 local_param.advanced_mode);
        return true;
    }

    auto& local_param = EnsureTaskParam(params_.param, tid);
    if (SetOpenAiConfigValue(local_param, key_str, param.value.ToString())) {
        std::string log_value = key_str.find("api_key") != std::string::npos ? "***" : param.value.ToString();
        LOG_INFO("[MODIFYPARAM] [{} {}] Task:{} set {}: {}", alg_code_, uuid, tid, key_str, log_value);
        return true;
    }
    return true;
}

bool Qwen3VLWorker::ModifyParam(const std::string& channel_id, const std::string& tid,
                                std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        std::string key_str = param.key.ToString();
        bool is_prompt_key  = (key_str == "keywords") || (key_str == "prompt") ||
                             (param.keys.size() >= 1 && param.keys[0] == "keywords") ||
                             (param.keys.size() >= 2 && param.keys[1] == "prompt");
        bool is_adv_mode_key = (key_str == "advanced_mode");
        bool is_openai_key   = (key_str == "vlmProvider") || (key_str == "provider") ||
                             (key_str.rfind("openai.", 0) == 0) || (key_str == "base_url") ||
                             (key_str == "api_key") || (key_str == "model") || (key_str == "endpoint") ||
                             (key_str == "timeout_ms") || (key_str == "max_tokens");
        if (is_prompt_key || is_adv_mode_key || is_openai_key) {
            AnalysisKey(channel_id, tid, param);
            continue;
        }
        if (!ValidKey(param))
            continue;
        AnalysisKey(channel_id, tid, param);
    }
    params_.param_modify_sign++;
    return true;
}

bool Qwen3VLWorker::SetParam(const std::string& channel_id, const std::string& tid,
                             std::vector<MsgDynamicKeyValue>& params) {
    std::string savedPrompt;
    bool savedAdvancedMode = false;
    {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = std::find_if(params_.param.begin(), params_.param.end(),
                               [&tid](const auto& p) { return p.task_id == tid; });
        if (it != params_.param.end()) {
            savedPrompt       = it->prompt;
            savedAdvancedMode = it->advanced_mode;
        }
        params_.param.clear();
    }
    bool ret = ModifyParam(channel_id, tid, params);
    if (!savedPrompt.empty() || savedAdvancedMode) {
        std::lock_guard<std::shared_mutex> lock(mtx);
        auto it = std::find_if(params_.param.begin(), params_.param.end(),
                               [&tid](const Qwen3VLWorkerParamEl& el) { return el.task_id == tid; });
        if (it == params_.param.end()) {
            Qwen3VLWorkerParamEl el;
            el.task_id       = tid;
            el.prompt        = savedPrompt;
            el.advanced_mode = savedAdvancedMode;
            params_.param.push_back(el);
        } else {
            if (it->prompt.empty() && !savedPrompt.empty()) {
                it->prompt = savedPrompt;
            }
            if (!it->advanced_mode && savedAdvancedMode) {
                // If the new params didn't explicitly set advanced_mode, restore the saved one.
                // We assume that if new params had it, it would be set in ModifyParam.
                // But we don't know if the new param specifically set it to diff value.
                // For safety and same behavior as old code, we don't strictly distinguish.
                it->advanced_mode = savedAdvancedMode;
            }
        }
    }
    return ret;
}

}  // namespace cosmo
