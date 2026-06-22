// OpenAiVlmClient — Open Ai Vlm Client implementation.

#include "flow/qwen3vl/OpenAiVlmClient.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <utility>

#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/network/IHttpClient.h"
#include "util/CipherUtil.h"
#include "util/Log.h"

static constexpr const char* kTag = "OpenAiVlm ";

namespace cosmo {
namespace {

    std::string TrimRightSlash(std::string value) {
        while (!value.empty() && value.back() == '/') {
            value.pop_back();
        }
        return value;
    }

    std::string NormalizeEndpoint(std::string value) {
        if (value.empty()) {
            return "/chat/completions";
        }
        if (value.front() != '/') {
            value.insert(value.begin(), '/');
        }
        return value;
    }

    std::string BuildUrl(const OpenAiVlmConfig& config) {
        return TrimRightSlash(config.base_url) + NormalizeEndpoint(config.endpoint);
    }

    std::string ToDataUrl(VideoFramePtr image) {
        auto jpeg = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().EncodeJpeg(image);
        if (jpeg.empty()) {
            return {};
        }
        return "data:image/jpeg;base64," + util::EncBase64Ex(jpeg.data(), jpeg.size());
    }

    std::string ExtractText(const nlohmann::json& response) {
        if (!response.contains("choices") || !response["choices"].is_array() || response["choices"].empty()) {
            return {};
        }

        const auto& choice = response["choices"][0];
        if (choice.contains("message") && choice["message"].contains("content")) {
            const auto& content = choice["message"]["content"];
            if (content.is_string()) {
                return content.get<std::string>();
            }
            if (content.is_array()) {
                std::string text;
                for (const auto& item : content) {
                    if (item.is_object() && item.value("type", "") == "text" && item.contains("text")) {
                        if (!text.empty()) {
                            text += "\n";
                        }
                        text += item["text"].get<std::string>();
                    }
                }
                return text;
            }
        }

        if (choice.contains("text") && choice["text"].is_string()) {
            return choice["text"].get<std::string>();
        }
        return {};
    }

}  // namespace

util::ErrorEnum OpenAiVlmClient::Generate(const OpenAiVlmConfig& config,
                                          const std::vector<VideoFramePtr>& images,
                                          const std::vector<std::string>& prompts,
                                          const Qwen3VLGenerationParam& gen_param,
                                          std::vector<Qwen3VLResult>& results) {
    if (!config.Enabled()) {
        return util::ErrorEnum::InvalidParam;
    }
    if (config.base_url.empty() || config.model.empty()) {
        LOG_WARN("{}missing base_url or model", kTag);
        return util::ErrorEnum::InvalidParam;
    }
    if (images.size() != prompts.size()) {
        LOG_WARN("{}images size({}) != prompts size({})", kTag, images.size(), prompts.size());
        return util::ErrorEnum::InvalidParam;
    }

    const auto url = BuildUrl(config);
    const long timeout_sec =
        std::max<long>(1L, static_cast<long>((std::max(config.timeout_ms, 1000) + 999) / 1000));

    for (size_t i = 0; i < images.size(); ++i) {
        if (!VideoFrameValid(images[i])) {
            return util::ErrorEnum::InvalidParam;
        }

        auto image_url = ToDataUrl(images[i]);
        if (image_url.empty()) {
            LOG_WARN("{}EncodeJpeg failed for image {}", kTag, i);
            return util::ErrorEnum::EncodeFailed;
        }

        nlohmann::json content = nlohmann::json::array();
        content.push_back({{"type", "text"}, {"text", prompts[i]}});
        content.push_back({{"type", "image_url"}, {"image_url", {{"url", image_url}}}});

        nlohmann::json body;
        body["model"]       = config.model;
        body["messages"]    = nlohmann::json::array({{{"role", "user"}, {"content", content}}});
        body["max_tokens"]  = config.max_tokens > 0 ? config.max_tokens : 256;
        body["temperature"] = gen_param.temperature;
        body["top_p"]       = gen_param.top_p;
        if (config.temperature >= 0.0f) {
            body["temperature"] = config.temperature;
        }
        if (config.top_p >= 0.0f) {
            body["top_p"] = config.top_p;
        }

        std::vector<std::pair<std::string, std::string>> headers;
        if (!config.api_key.empty()) {
            headers.emplace_back("Authorization", "Bearer " + config.api_key);
        }

        auto response = service::ServiceRegistry::Instance().Get<service::IHttpClient>().Post(
            url, body.dump(), "application/json", 3, timeout_sec, headers);
        if (response.statusCode < 200 || response.statusCode >= 300) {
            LOG_WARN("{}request failed. status:{} body:{}", kTag, response.statusCode, response.body);
            return util::ErrorEnum::AI_FORWARD_FAILED;
        }

        nlohmann::json response_json;
        try {
            response_json = nlohmann::json::parse(response.body);
        } catch (const std::exception& e) {
            LOG_WARN("{}response json parse failed: {}", kTag, e.what());
            return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
        }

        Qwen3VLResult result;
        result.text        = ExtractText(response_json);
        result.frame_index = static_cast<int64_t>(images[i]->GetFrameIndex());
        result.timestamp   = images[i]->GetTimestamp();
        if (result.text.empty()) {
            LOG_WARN("{}empty text in response: {}", kTag, response.body);
        }
        results.push_back(std::move(result));
    }

    return util::ErrorEnum::Success;
}

}  // namespace cosmo
