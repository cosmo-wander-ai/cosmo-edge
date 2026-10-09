#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "nn/core/blob.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_image_utils.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_predict_utils.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_runner.h"
#include "stb/stb_image.h"
#include "tokenizers_cpp.h"

namespace {
using Json   = nlohmann::json;
using Runner = cosmo::nn::Qwen3VLRunner;

void Check(cosmo::nn::Status status) {
    if (status != cosmo::nn::COSMO_NN_OK)
        throw std::runtime_error(status.description());
}

Json CheckInput(tokenizers::Tokenizer& tokenizer, const Json& request) {
    Json response = {{"type", "input_check"},
                     {"id", request.value("id", Json(nullptr))},
                     {"status", "input_error"},
                     {"inference_executed", false}};
    try {
        const auto prefix = request.at("input_dump_prefix").get<std::string>();
        if (prefix.empty())
            throw std::invalid_argument("input_dump_prefix must not be empty");
        const auto path   = request.at("image").get<std::string>();
        const auto prompt = request.at("prompt").get<std::string>();
        if (prompt.empty())
            throw std::invalid_argument("prompt must not be empty");
        int width = 0, height = 0, channels = 0;
        using Image = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>;
        Image pixels(stbi_load(path.c_str(), &width, &height, &channels, 3), stbi_image_free);
        if (!pixels || width <= 0 || height <= 0)
            throw std::runtime_error("image decode failed");
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
            std::swap(pixels.get()[i * 3], pixels.get()[i * 3 + 2]);
        cosmo::nn::qwen3vl::Config config;
        config.evaluation_square_448 = true;
        std::vector<float> values;
        if (!cosmo::nn::qwen3vl::process_image_from_mat(pixels.get(), width, height, width * 3, config,
                                                        values))
            throw std::runtime_error("image preprocessing failed");
        const auto rendered = cosmo::nn::qwen3vl::BuildImagePrompt(prompt, {config.grid_thw}, true, true);
        const auto tokens   = tokenizer.Encode(rendered);
        std::ofstream pixel_file(prefix + ".pixels.f32", std::ios::binary);
        pixel_file.write(reinterpret_cast<const char*>(values.data()),
                         static_cast<std::streamsize>(values.size() * sizeof(float)));
        std::ofstream token_file(prefix + ".input.json");
        token_file << Json({{"input_ids", tokens},
                            {"grid_thw", config.grid_thw},
                            {"rendered_prompt", rendered}})
                          .dump(2);
        pixel_file.close();
        token_file.close();
        if (!pixel_file || !token_file)
            throw std::runtime_error("failed to save input tensors");
        response.update({{"status", "ok"},
                         {"input_tokens", tokens.size()},
                         {"pixel_elements", values.size()},
                         {"grid_thw", config.grid_thw}});
    } catch (const std::exception& error) {
        response["error"] = error.what();
    }
    return response;
}

Json Run(Runner& runner, const Json& request, const Json& effective, Runner::EvaluationOptions options,
         bool legacy = false) {
    const auto start       = std::chrono::steady_clock::now();
    Json response          = {{"type", "result"}, {"id", request.at("id")}, {"status", "inference_error"},
                              {"raw_output", ""}, {"error", nullptr},       {"effective_config", effective}};
    bool inference_started = false;
    if (legacy)
        response["type"] = "legacy_check";
    try {
        const auto path = request.at("image").get<std::string>();
        auto prompt     = request.at("prompt").get<std::string>();
        int width = 0, height = 0, channels = 0;
        using Image = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>;
        Image pixels(stbi_load(path.c_str(), &width, &height, &channels, 3), stbi_image_free);
        if (!pixels || width <= 0 || height <= 0)
            throw std::runtime_error("image decode failed");
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
            std::swap(pixels.get()[i * 3], pixels.get()[i * 3 + 2]);
        cosmo::nn::BlobDesc image_desc;
        image_desc.device_type = cosmo::nn::DEVICE_NAIVE;
        image_desc.dims        = {1, height, width, 3};
        cosmo::nn::BlobHandle image_handle;
        image_handle.base = pixels.get();
        cosmo::nn::BlobDesc prompt_desc;
        prompt_desc.dims = {1, static_cast<int>(prompt.size())};
        cosmo::nn::BlobHandle prompt_handle;
        prompt_handle.base        = prompt.data();
        auto image_blob           = std::make_shared<cosmo::nn::Blob>(image_desc, image_handle);
        auto prompt_blob          = std::make_shared<cosmo::nn::Blob>(prompt_desc, prompt_handle);
        options.input_dump_prefix = request.value("input_dump_prefix", "");
        if (!legacy)
            Check(runner.ConfigureEvaluation(options));
        inference_started = true;
        Check(runner.Run({{image_blob}, {prompt_blob}}));
        const auto& result = runner.GetEvaluationResult();
        response.update({{"status", result.raw_output.empty() ? "empty_output" : "ok"},
                         {"raw_output", result.raw_output},
                         {"stop_reason", result.stop_reason},
                         {"truncated", result.truncated},
                         {"input_tokens", result.input_tokens},
                         {"output_tokens", result.output_tokens},
                         {"inference_ms", result.inference_ms}});
        if (legacy) {
            const auto& outputs         = runner.GetTextOutputs();
            response["business_output"] = outputs.empty() || outputs[0].empty() ? "" : outputs[0][0];
        }
    } catch (const std::exception& error) {
        if (inference_started) {
            response["raw_output"]    = runner.GetEvaluationResult().raw_output;
            response["output_tokens"] = runner.GetEvaluationResult().output_tokens;
        }
        response["error"]       = error.what();
        response["stop_reason"] = "error";
        response["truncated"]   = nullptr;
    }
    response["device_request_ms"] =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return response;
}
}  // namespace

int main(int argc, char** argv) {
    // Vendor runtimes may print to stdout; isolate the protocol on the original descriptor.
    const int protocol_fd = dup(STDOUT_FILENO);
    if (protocol_fd < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
        return 2;
    FILE* protocol = fdopen(protocol_fd, "w");
    if (!protocol)
        return 2;
    const auto emit = [protocol](const Json& value) {
        const auto line = value.dump() + "\n";
        if (std::fwrite(line.data(), 1, line.size(), protocol) != line.size() || std::fflush(protocol) != 0)
            throw std::runtime_error("protocol output failed");
    };
    try {
        if (argc != 3 || (std::string(argv[1]) != "--config" && std::string(argv[1]) != "--check-input" &&
                          std::string(argv[1]) != "--legacy-check"))
            throw std::invalid_argument(
                "usage: cosmo-vlm-eval --config|--check-input|--legacy-check CONFIG.json");
        std::ifstream config_file(argv[2]);
        const Json config = Json::parse(config_file);
        if (std::string(argv[1]) == "--check-input") {
            if (config.value("model_type", "qwen3_5") != "qwen3_5")
                throw std::invalid_argument("input check supports the frozen qwen3_5 protocol only");
            std::ifstream file(config.at("tokenizer_path").get<std::string>(), std::ios::binary);
            const std::string blob((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (blob.empty())
                throw std::runtime_error("failed to read tokenizer file");
            auto tokenizer = tokenizers::Tokenizer::FromBlobJSON(blob);
            if (!tokenizer)
                throw std::runtime_error("failed to load tokenizer JSON");
            emit({{"type", "input_check_ready"}, {"inference_executed", false}});
            std::string line;
            while (std::getline(std::cin, line)) {
                try {
                    emit(CheckInput(*tokenizer, Json::parse(line)));
                } catch (const std::exception& error) {
                    emit({{"type", "input_check"},
                          {"id", nullptr},
                          {"status", "input_error"},
                          {"inference_executed", false},
                          {"error", error.what()}});
                }
            }
            std::fclose(protocol);
            return 0;
        }
        Runner runner;
        Check(runner.Init(config.at("model_path"), config.at("tokenizer_path"), config.value("device_id", 0),
                          "{\"generation\":{\"do_sample\":false}}", config.value("model_type", "qwen3_5")));
        Runner::EvaluationOptions options;
        if (std::string(argv[1]) == "--legacy-check") {
            const Json effective = {{"mode", "legacy_business_check"},
                                    {"backend", "sophon"},
                                    {"preprocessing", "legacy_smart_resize"},
                                    {"generation_budget", "artifact_and_business_defaults"},
                                    {"artifact_context_length", runner.GetContextLength()},
                                    {"max_input_length", runner.GetMaxInputLength()},
                                    {"do_sample", false}};
            emit({{"type", "legacy_check_ready"}, {"effective_config", effective}});
            std::string line;
            while (std::getline(std::cin, line)) {
                try {
                    emit(Run(runner, Json::parse(line), effective, options, true));
                } catch (const std::exception& error) {
                    emit({{"type", "legacy_check"},
                          {"id", nullptr},
                          {"status", "invalid_request"},
                          {"raw_output", ""},
                          {"error", error.what()}});
                }
            }
            std::fclose(protocol);
            return 0;
        }
        options.max_new_tokens = config.at("max_new_tokens");
        options.context_length = config.at("context_length");
        options.trace_networks = config.value("trace_networks", false);
        Check(runner.ConfigureEvaluation(options));
        const Json effective = {{"backend", "sophon"},
                                {"model_type", config.value("model_type", "qwen3_5")},
                                {"max_new_tokens", options.max_new_tokens},
                                {"context_length", options.context_length},
                                {"artifact_context_length", runner.GetContextLength()},
                                {"max_input_length", runner.GetMaxInputLength()},
                                {"do_sample", false},
                                {"thinking", false},
                                {"keep_history", false},
                                {"trace_networks", options.trace_networks},
                                {"preprocessing", "inspecsafe_rgb_en_joint_v1"},
                                {"resize", "half_pixel_bilinear_float32_lround"},
                                {"image_size", 448}};
        emit({{"type", "ready"}, {"effective_config", effective}});
        std::string line;
        while (std::getline(std::cin, line)) {
            try {
                emit(Run(runner, Json::parse(line), effective, options));
            } catch (const std::exception& error) {
                emit({{"type", "result"},
                      {"id", nullptr},
                      {"status", "invalid_request"},
                      {"raw_output", ""},
                      {"error", error.what()}});
            }
        }
        std::fclose(protocol);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 2;
    }
}
