#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "infer/Qwen3VLUnify.h"
#include "infer/RkllmVlmBackend.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "stb/stb_image.h"

namespace {
using Json = nlohmann::json;

void Check(cosmo::util::ErrorEnum code, const char* operation) {
    if (code != cosmo::util::ErrorEnum::Success)
        throw std::runtime_error(std::string(operation) +
                                 " failed: " + std::to_string(static_cast<int>(code)));
}

Json Run(cosmo::RkllmVlmBackend& backend, const Json& request, bool legacy = false) {
    const auto started = std::chrono::steady_clock::now();
    Json result        = {{"type", "result"}, {"id", request.at("id")},   {"status", "inference_error"},
                          {"raw_output", ""}, {"stop_reason", "unknown"}, {"truncated", nullptr}};
    if (legacy)
        result["type"] = "legacy_check";
    try {
        const auto path   = request.at("image").get<std::string>();
        const auto prompt = request.at("prompt").get<std::string>();
        int width = 0, height = 0, channels = 0;
        using Image = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>;
        Image rgb(stbi_load(path.c_str(), &width, &height, &channels, 3), stbi_image_free);
        if (!rgb || width <= 0 || height <= 0)
            throw std::runtime_error("image decode failed");
        const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
        if (legacy) {
            if (bytes > static_cast<size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("legacy frame is too large");
            cosmo::mem::MemoryPoolMng pool(std::make_unique<cosmo::mem::AllocatorCpu>(),
                                           {static_cast<int>(bytes)});
            cosmo::mem::SetMemoryPoolContext(&pool);
            struct ResetContext {
                ~ResetContext() {
                    cosmo::mem::SetMemoryPoolContext(nullptr);
                }
            } reset;
            auto frame = std::make_shared<cosmo::media::VideoFrame>(width, height,
                                                                    cosmo::media::PixelFormat::PIXEL_RGB8);
            if (!VideoFrameValid(frame) || !frame->GetData())
                throw std::runtime_error("legacy frame allocation failed");
            std::memcpy(frame->GetData(), rgb.get(), bytes);
            cosmo::Qwen3VLGenerationParam generation;
            std::vector<cosmo::Qwen3VLResult> outputs;
            Check(backend.Generate({frame}, {prompt}, generation, outputs), "legacy generate");
            if (outputs.size() != 1)
                throw std::runtime_error("legacy result count mismatch");
            result.update({{"status", outputs[0].text.empty() ? "empty_output" : "ok"},
                           {"raw_output", outputs[0].text},
                           {"business_output", outputs[0].text}});
        } else {
            result.update(backend.EvaluateRgb(rgb.get(), bytes, width, height, prompt,
                                              request.value("input_dump_prefix", "")));
        }
        result["type"] = legacy ? "legacy_check" : "result";
        result["id"]   = request.at("id");
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    result["device_request_ms"] =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}
}  // namespace

int main(int argc, char** argv) {
    // Keep all vendor/library stdout off the JSONL protocol channel.
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
        if (argc != 3 || (std::string(argv[1]) != "--config" && std::string(argv[1]) != "--legacy-check"))
            throw std::invalid_argument("usage: cosmo-vlm-eval --config|--legacy-check CONFIG.json");
        std::ifstream input(argv[2]);
        const Json config = Json::parse(input);
        if (!config.contains("backend") || !config["backend"].is_string() ||
            config["backend"].get<std::string>() != "rkllm")
            throw std::invalid_argument("RK evaluation config requires backend=rkllm");
        cosmo::RkllmVlmBackend backend(config.at("model_path").get<std::string>());
        if (std::string(argv[1]) == "--legacy-check") {
            Check(backend.Init(), "initialize legacy RKLLM/RKNN");
            emit({{"type", "legacy_check_ready"},
                  {"effective_config",
                   {{"backend", "rkllm"},
                    {"mode", "legacy_business_check"},
                    {"max_new_tokens", 2},
                    {"context_length", 512},
                    {"preprocessing", "legacy_backend_resize"},
                    {"generation", "Qwen3VLGenerationParam defaults"}}}});
            std::string line;
            while (std::getline(std::cin, line)) {
                try {
                    const auto result = Run(backend, Json::parse(line), true);
                    emit(result);
                    if (result.value("status", "inference_error") != "ok") {
                        std::fclose(protocol);
                        return 3;
                    }
                } catch (const std::exception& error) {
                    emit({{"type", "legacy_check"},
                          {"id", nullptr},
                          {"status", "invalid_request"},
                          {"raw_output", ""},
                          {"error", error.what()}});
                    std::fclose(protocol);
                    return 2;
                }
            }
            std::fclose(protocol);
            return 0;
        }
        cosmo::RkllmVlmBackend::EvaluationOptions options;
        options.max_new_tokens        = config.at("max_new_tokens").get<int>();
        options.context_length        = config.at("context_length").get<int>();
        options.expected_input_tokens = config.at("expected_input_tokens").get<int>();
        options.vision_normalization  = config.at("vision_normalization").get<std::string>();
        Check(backend.ConfigureEvaluation(options), "configure evaluation");
        Check(backend.Init(), "initialize RKLLM/RKNN");
        emit({{"type", "ready"}, {"effective_config", backend.GetEvaluationMetadata()}});
        std::string line;
        while (std::getline(std::cin, line)) {
            try {
                const auto result = Run(backend, Json::parse(line));
                emit(result);
                if (result.value("process_restart_required", false)) {
                    std::fclose(protocol);
                    return 3;
                }
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
