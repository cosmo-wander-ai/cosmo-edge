#include "infer/RkllmVlmBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "infer/Qwen3VLUnify.h"
#include "infer/VlmEvaluationImage.h"
#include "infer/VlmEvaluationTokens.h"
#include "media/PixelFormat.h"
#include "nn/guard/ModelLoadPolicy.h"
#ifdef COSMO_HAS_MODEL_GUARD
#include "nn/guard/CemRknnV1Loader.h"
#endif
#include "rkllm.h"
#include "rknn_api.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo {
namespace {

    struct ImageEncoderContext {
        rknn_context ctx{0};
        rknn_input_output_num io_num{};
        std::vector<rknn_tensor_attr> inputs;
        std::vector<rknn_tensor_attr> outputs;
        int width{0};
        int height{0};
        int channels{0};
        int image_tokens{0};
        int embed_size{0};
    };

    struct RunContext {
        std::string text;
        bool failed{false};
        bool evaluation{false};
        bool finished{false};
        int last_state{-1};
        bool has_perf{false};
        RKLLMPerfStat perf{};
        std::vector<int32_t> token_ids;
        std::vector<int> states;
        std::mutex mutex;
    };

    int RkllmResultCallback(RKLLMResult* result, void* userdata, LLMCallState state) {
        auto* context = static_cast<RunContext*>(userdata);
        if (!context) {
            return 0;
        }
        if (context->evaluation) {
            std::lock_guard<std::mutex> lock(context->mutex);
            context->last_state = static_cast<int>(state);
            context->states.push_back(static_cast<int>(state));
            if (state == RKLLM_RUN_ERROR)
                context->failed = true;
            if (state == RKLLM_RUN_NORMAL && result) {
                if (result->text)
                    context->text.append(result->text);
                context->token_ids.push_back(result->token_id);
            }
            if (state == RKLLM_RUN_FINISH) {
                context->finished = true;
                if (result && result->perf.prefill_tokens > 0 && result->perf.generate_tokens >= 0 &&
                    std::isfinite(result->perf.prefill_time_ms) && result->perf.prefill_time_ms >= 0 &&
                    std::isfinite(result->perf.generate_time_ms) && result->perf.generate_time_ms >= 0 &&
                    std::isfinite(result->perf.memory_usage_mb) && result->perf.memory_usage_mb >= 0) {
                    context->perf     = result->perf;
                    context->has_perf = true;
                }
            }
            return 0;
        }
        if (state == RKLLM_RUN_ERROR) {
            context->failed = true;
        } else if (state == RKLLM_RUN_NORMAL && result && result->text) {
            context->text.append(result->text);
        }
        return 0;
    }

    void ReleaseImageEncoder(ImageEncoderContext& encoder) {
        if (encoder.ctx != 0) {
            rknn_destroy(encoder.ctx);
            encoder.ctx = 0;
        }
        encoder.inputs.clear();
        encoder.outputs.clear();
    }

    bool LoadImageEncoderContext(const std::string& path, rknn_context& context) {
        const auto decision = nn::ModelLoadPolicy::Production().Evaluate(path, nn::ModelLoadIntent::kRawRknn);
        if (!decision.IsAllowed()) {
            LOG_ERRO("RKLLM vision encoder format rejected. path:{}", path);
            return false;
        }
        if (decision.action == nn::ModelLoadAction::kGuardV2) {
#ifdef COSMO_HAS_MODEL_GUARD
            const std::string certificate_path =
                (std::filesystem::path(cosmo::path::GetBaseDir()) / "model-guard" / "device-certificate.bin")
                    .string();
            auto loaded = nn::LoadCemRknnV1Artifact(nn::FrozenCmgRknnV1Api(), nn::NativeRknnContextApi(),
                                                    decision.model_path.c_str(), certificate_path.c_str());
            if (!loaded.IsSuccess() || loaded.contexts.size() != 1 || loaded.contexts.front().Get() == 0) {
                LOG_ERRO("RKLLM protected vision encoder load failed. path:{} status:{}", path,
                         loaded.guard_status);
                return false;
            }
            context = static_cast<rknn_context>(loaded.contexts.front().Release());
            return true;
#else
            LOG_ERRO("RKLLM protected vision encoder requires model-guard. path:{}", path);
            return false;
#endif
        }
        // A protected-model failure must never fall back to the native loader.
        if (decision.action != nn::ModelLoadAction::kNativeRawRknn) {
            return false;
        }
        int ret = rknn_init(&context, const_cast<char*>(decision.model_path.c_str()), 0, 0, nullptr);
        if (ret != RKNN_SUCC) {
            LOG_ERRO("RKLLM vision encoder init failed. path:{} ret:{}", path, ret);
            if (context != 0) {
                rknn_destroy(context);
                context = 0;
            }
            return false;
        }
        return context != 0;
    }

    bool InitImageEncoder(const std::string& path, ImageEncoderContext& encoder) {
        if (!LoadImageEncoderContext(path, encoder.ctx)) {
            return false;
        }

        int ret = rknn_set_core_mask(encoder.ctx, RKNN_NPU_CORE_0_1);
        if (ret != RKNN_SUCC) {
            LOG_ERRO("RKLLM vision encoder core selection failed. ret:{}", ret);
            ReleaseImageEncoder(encoder);
            return false;
        }

        ret = rknn_query(encoder.ctx, RKNN_QUERY_IN_OUT_NUM, &encoder.io_num, sizeof(encoder.io_num));
        if (ret != RKNN_SUCC || encoder.io_num.n_input != 1 || encoder.io_num.n_output < 1) {
            LOG_ERRO("RKLLM invalid vision encoder IO. ret:{} inputs:{} outputs:{}", ret,
                     encoder.io_num.n_input, encoder.io_num.n_output);
            ReleaseImageEncoder(encoder);
            return false;
        }

        encoder.inputs.resize(encoder.io_num.n_input);
        for (uint32_t i = 0; i < encoder.io_num.n_input; ++i) {
            encoder.inputs[i]       = {};
            encoder.inputs[i].index = i;
            if (rknn_query(encoder.ctx, RKNN_QUERY_INPUT_ATTR, &encoder.inputs[i],
                           sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
                LOG_ERRO("RKLLM vision encoder input query failed. index:{}", i);
                ReleaseImageEncoder(encoder);
                return false;
            }
        }

        encoder.outputs.resize(encoder.io_num.n_output);
        for (uint32_t i = 0; i < encoder.io_num.n_output; ++i) {
            encoder.outputs[i]       = {};
            encoder.outputs[i].index = i;
            if (rknn_query(encoder.ctx, RKNN_QUERY_OUTPUT_ATTR, &encoder.outputs[i],
                           sizeof(rknn_tensor_attr)) != RKNN_SUCC) {
                LOG_ERRO("RKLLM vision encoder output query failed. index:{}", i);
                ReleaseImageEncoder(encoder);
                return false;
            }
        }

        const auto& input = encoder.inputs.front();
        if (input.fmt == RKNN_TENSOR_NCHW) {
            encoder.channels = input.dims[1];
            encoder.height   = input.dims[2];
            encoder.width    = input.dims[3];
        } else {
            encoder.height   = input.dims[1];
            encoder.width    = input.dims[2];
            encoder.channels = input.dims[3];
        }

        const auto& output = encoder.outputs.front();
        for (uint32_t i = 0; i + 1 < output.n_dims; ++i) {
            if (output.dims[i] > 1 && output.dims[i + 1] > 1) {
                encoder.image_tokens = output.dims[i];
                encoder.embed_size   = output.dims[i + 1];
                break;
            }
        }
        if (encoder.width <= 0 || encoder.height <= 0 || encoder.channels != 3 || encoder.image_tokens <= 0 ||
            encoder.embed_size <= 0) {
            LOG_ERRO("RKLLM unsupported vision encoder shape. image:{}x{}x{} tokens:{} embed:{}",
                     encoder.width, encoder.height, encoder.channels, encoder.image_tokens,
                     encoder.embed_size);
            ReleaseImageEncoder(encoder);
            return false;
        }

        LOG_INFO("RKLLM vision encoder ready. path:{} image:{}x{} tokens:{} embed:{} outputs:{}", path,
                 encoder.width, encoder.height, encoder.image_tokens, encoder.embed_size,
                 encoder.io_num.n_output);
        return true;
    }

    bool ResizeFrameToRgb(const VideoFramePtr& frame, int dst_width, int dst_height,
                          std::vector<uint8_t>& output) {
        const int src_width  = static_cast<int>(frame->GetWidth());
        const int src_height = static_cast<int>(frame->GetHeight());
        auto* src            = frame->GetHostData() ? frame->GetHostData() : frame->GetData();
        if (!src || src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0) {
            return false;
        }

        const int square_size = std::max(src_width, src_height);
        const int x_offset    = (square_size - src_width) / 2;
        const int y_offset    = (square_size - src_height) / 2;
        const bool input_bgr  = frame->GetPixelFormat() == media::PixelFormat::PIXEL_BGR8;
        const bool input_rgb  = frame->GetPixelFormat() == media::PixelFormat::PIXEL_RGB8;
        if (!input_bgr && !input_rgb) {
            return false;
        }

        output.resize(static_cast<size_t>(dst_width) * dst_height * 3);
        std::fill(output.begin(), output.end(), 128);
        auto channel = [&](int x, int y, int c) -> float {
            if (x < x_offset || x >= x_offset + src_width || y < y_offset || y >= y_offset + src_height) {
                return 127.5F;
            }
            const int sx             = x - x_offset;
            const int sy             = y - y_offset;
            const int source_channel = input_bgr ? (2 - c) : c;
            return static_cast<float>(src[(static_cast<size_t>(sy) * src_width + sx) * 3 + source_channel]);
        };

        for (int y = 0; y < dst_height; ++y) {
            const float square_y = (static_cast<float>(y) + 0.5F) * square_size / dst_height - 0.5F;
            const int y0         = static_cast<int>(std::floor(square_y));
            const int y1         = y0 + 1;
            const float fy       = square_y - y0;
            for (int x = 0; x < dst_width; ++x) {
                const float square_x = (static_cast<float>(x) + 0.5F) * square_size / dst_width - 0.5F;
                const int x0         = static_cast<int>(std::floor(square_x));
                const int x1         = x0 + 1;
                const float fx       = square_x - x0;
                for (int c = 0; c < 3; ++c) {
                    const float v00 =
                        channel(std::clamp(x0, 0, square_size - 1), std::clamp(y0, 0, square_size - 1), c);
                    const float v01 =
                        channel(std::clamp(x1, 0, square_size - 1), std::clamp(y0, 0, square_size - 1), c);
                    const float v10 =
                        channel(std::clamp(x0, 0, square_size - 1), std::clamp(y1, 0, square_size - 1), c);
                    const float v11 =
                        channel(std::clamp(x1, 0, square_size - 1), std::clamp(y1, 0, square_size - 1), c);
                    const float value =
                        (v00 * (1.0F - fx) + v01 * fx) * (1.0F - fy) + (v10 * (1.0F - fx) + v11 * fx) * fy;
                    output[(static_cast<size_t>(y) * dst_width + x) * 3 + c] =
                        static_cast<uint8_t>(std::clamp(value, 0.0F, 255.0F));
                }
            }
        }
        return true;
    }

    bool EncodeImage(ImageEncoderContext& encoder, const std::vector<uint8_t>& image,
                     std::vector<float>& embedding) {
        rknn_input input{};
        input.index = 0;
        input.type  = RKNN_TENSOR_UINT8;
        input.fmt   = RKNN_TENSOR_NHWC;
        input.size  = static_cast<uint32_t>(image.size());
        input.buf   = const_cast<uint8_t*>(image.data());
        if (rknn_inputs_set(encoder.ctx, 1, &input) != RKNN_SUCC ||
            rknn_run(encoder.ctx, nullptr) != RKNN_SUCC) {
            LOG_ERRO("{}", "RKLLM vision encoder inference failed");
            return false;
        }

        std::vector<rknn_output> outputs(encoder.io_num.n_output);
        for (uint32_t i = 0; i < encoder.io_num.n_output; ++i) {
            outputs[i]            = {};
            outputs[i].index      = i;
            outputs[i].want_float = 1;
        }
        if (rknn_outputs_get(encoder.ctx, encoder.io_num.n_output, outputs.data(), nullptr) != RKNN_SUCC) {
            LOG_ERRO("{}", "RKLLM vision encoder output retrieval failed");
            return false;
        }

        const size_t per_output = static_cast<size_t>(encoder.image_tokens) * encoder.embed_size;
        embedding.resize(per_output * encoder.io_num.n_output);
        if (encoder.io_num.n_output == 1) {
            const size_t available = outputs[0].size / sizeof(float);
            std::memcpy(embedding.data(), outputs[0].buf,
                        std::min(embedding.size(), available) * sizeof(float));
        } else {
            for (int token = 0; token < encoder.image_tokens; ++token) {
                for (uint32_t j = 0; j < encoder.io_num.n_output; ++j) {
                    const auto* source = static_cast<const float*>(outputs[j].buf) +
                                         static_cast<size_t>(token) * encoder.embed_size;
                    auto* destination =
                        embedding.data() +
                        (static_cast<size_t>(token) * encoder.io_num.n_output + j) * encoder.embed_size;
                    std::memcpy(destination, source, encoder.embed_size * sizeof(float));
                }
            }
        }
        rknn_outputs_release(encoder.ctx, encoder.io_num.n_output, outputs.data());
        return true;
    }

}  // namespace

struct RkllmVlmBackend::Impl {
    LLMHandle llm{nullptr};
    RKLLMCallback callback{};
    ImageEncoderContext encoder;
    std::unique_ptr<RunContext> evaluation_run;
    std::string evaluation_prompt;
    std::vector<float> evaluation_embedding;
    RKLLMInput evaluation_input{};
    RKLLMSamplingParam evaluation_sampling{};
    RKLLMInferParam evaluation_infer{};
    bool evaluation_poisoned{false};
};

RkllmVlmBackend::RkllmVlmBackend(std::string model_path) : model_path_(std::move(model_path)) {}

RkllmVlmBackend::~RkllmVlmBackend() {
    if (!impl_) {
        return;
    }
    ReleaseImageEncoder(impl_->encoder);
    if (impl_->llm) {
        rkllm_destroy(impl_->llm);
        impl_->llm = nullptr;
    }
    delete impl_;
    impl_ = nullptr;
}

util::ErrorEnum RkllmVlmBackend::ConfigureGeneration(int max_new_tokens, int context_length) {
    if (impl_ || max_new_tokens <= 0 || context_length <= max_new_tokens)
        return util::ErrorEnum::InvalidParam;
    max_new_tokens_ = max_new_tokens;
    context_length_ = context_length;
    return util::ErrorEnum::Success;
}

util::ErrorEnum RkllmVlmBackend::Init() {
    if (impl_) {
        return util::ErrorEnum::Created;
    }
    const std::filesystem::path configured_path(model_path_);
    const auto model_dir =
        std::filesystem::is_directory(configured_path) ? configured_path : configured_path.parent_path();
    const auto llm_path =
        configured_path.extension() == ".rkllm" ? configured_path : model_dir / "model.rkllm";
    const auto vision_path = model_dir / "vision.rknn";
    if (!std::filesystem::is_regular_file(llm_path) || !std::filesystem::is_regular_file(vision_path)) {
        LOG_ERRO("RKLLM model files missing. llm:{} vision:{}", llm_path.string(), vision_path.string());
        return util::ErrorEnum::FileNotExist;
    }
    // ModelPathMapper may select vision.rknn after the visual encoder is added to the
    // model directory. RKLLM must always receive the adjacent language model instead.
    model_path_ = llm_path.string();

    std::unique_ptr<Impl> candidate(new Impl());
    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = model_path_.c_str();
    param.top_k      = 1;
    // Retain the business defaults unless explicitly configured before initialization.
    param.max_new_tokens     = max_new_tokens_;
    param.max_context_len    = context_length_;
    param.skip_special_token = true;
    if (evaluation_enabled_) {
        param.top_p             = 1.0F;
        param.temperature       = 0.0F;
        param.repeat_penalty    = 1.0F;
        param.frequency_penalty = 0.0F;
        param.presence_penalty  = 0.0F;
        param.ignore_eos_token  = false;
    }
    param.extend_param.base_domain_id   = 1;
    candidate->callback.result_callback = RkllmResultCallback;
    if (rkllm_init(&candidate->llm, &param, &candidate->callback) != 0) {
        LOG_ERRO("RKLLM language model init failed. path:{}", model_path_);
        return util::ErrorEnum::Failed;
    }
    if (!InitImageEncoder(vision_path.string(), candidate->encoder)) {
        rkllm_destroy(candidate->llm);
        candidate->llm = nullptr;
        return util::ErrorEnum::Failed;
    }

    if (evaluation_enabled_) {
        const auto& encoder = candidate->encoder;
        const auto& input   = encoder.inputs.front();
        const auto& output  = encoder.outputs.front();
        if (encoder.io_num.n_input != 1 || encoder.io_num.n_output != 1 || input.n_dims != 4 ||
            (input.fmt != RKNN_TENSOR_NCHW && input.fmt != RKNN_TENSOR_NHWC) ||
            (output.type != RKNN_TENSOR_FLOAT16 && output.type != RKNN_TENSOR_FLOAT32) ||
            input.n_elems != 448 * 448 * 3 || encoder.width != 448 || encoder.height != 448 ||
            encoder.channels != 3 || encoder.image_tokens != 196 || encoder.embed_size != 1024 ||
            output.n_elems != 196 * 1024) {
            ReleaseImageEncoder(candidate->encoder);
            rkllm_destroy(candidate->llm);
            candidate->llm = nullptr;
            return util::ErrorEnum::InvalidParam;
        }
    }
    impl_ = candidate.release();
    LOG_INFO("RKLLM multimodal backend initialized. llm:{} vision:{}", model_path_, vision_path.string());
    return util::ErrorEnum::Success;
}

util::ErrorEnum RkllmVlmBackend::ConfigureEvaluation(const EvaluationOptions& options) {
    if (impl_ || options.max_new_tokens <= 0 || options.context_length <= 0 ||
        options.vision_normalization != "embedded_mean127.5_std127.5")
        return util::ErrorEnum::InvalidParam;
    const auto ret = ConfigureGeneration(options.max_new_tokens, options.context_length);
    if (ret != util::ErrorEnum::Success)
        return ret;
    evaluation_options_ = options;
    evaluation_enabled_ = true;
    return util::ErrorEnum::Success;
}

nlohmann::json RkllmVlmBackend::GetEvaluationMetadata() const {
    nlohmann::json result = {
        {"backend", "rkllm"},
        {"evaluation_enabled", evaluation_enabled_},
        {"max_new_tokens", max_new_tokens_},
        {"context_length", context_length_},
        {"context_limit_source", "parameters_passed_to_rkllm_init_not_queryable"},
        {"expected_input_tokens", evaluation_options_.expected_input_tokens},
        {"input_token_policy", "platform_observed_count_within_context; reference_match_not_required"},
        {"do_sample", false},
        {"top_k", 1},
        {"top_p", 1.0},
        {"temperature", 0.0},
        {"repeat_penalty", 1.0},
        {"thinking", false},
        {"keep_history", false},
        {"skip_special_token", true},
        {"ignore_eos_token", false},
        {"preprocessing", "inspecsafe_rgb_en_joint_v1"},
        {"image_size", 448},
        {"vision_input_type", "uint8_rgb_nhwc"},
        {"vision_normalization", evaluation_options_.vision_normalization},
        {"normalization_source", "artifact_declaration_requires_external_parity_validation"},
        {"input_token_ids_available", false},
        {"chat_template_source", "SDK internal artifact template; platform-specific deployment"},
        {"prompt_trailing_ascii_whitespace", "removed"},
        {"sdk_memory_source", "RKLLMPerfStat.memory_usage_mb: VmHWM in MB"},
        {"output_tokens_source", "RKLLMPerfStat.generate_tokens; SDK-reported convention"},
        {"callback_output_tokens_source", "NORMAL result callbacks; FINISH token_id excluded"},
        {"output_budget_reached_source", "callback_output_tokens >= max_new_tokens; not truncation proof"},
        {"stop_reason_capability", "SDK exposes finish/error but not EOS versus output limit"}};
    if (impl_) {
        result["vision_image_tokens"]       = impl_->encoder.image_tokens;
        result["vision_embedding_size"]     = impl_->encoder.embed_size;
        result["vision_outputs"]            = impl_->encoder.io_num.n_output;
        result["vision_native_input_type"]  = static_cast<int>(impl_->encoder.inputs.front().type);
        result["vision_native_output_type"] = static_cast<int>(impl_->encoder.outputs.front().type);
    }
    return result;
}

int RkllmVlmBackend::IsEvaluationRunning() const {
    return impl_ && impl_->llm ? rkllm_is_running(impl_->llm) : -1;
}

int RkllmVlmBackend::AbortEvaluation() {
    return impl_ && impl_->llm && evaluation_enabled_ ? rkllm_abort(impl_->llm) : -1;
}

nlohmann::json RkllmVlmBackend::Evaluate(const VideoFramePtr& image, const std::string& prompt,
                                         const std::string& input_dump_prefix) {
    if (!image || !VideoFrameValid(image) ||
        (image->GetPixelFormat() != media::PixelFormat::PIXEL_RGB8 &&
         image->GetPixelFormat() != media::PixelFormat::PIXEL_BGR8))
        return EvaluatePacked(nullptr, 0, 0, 0, false, prompt, input_dump_prefix);
    return EvaluatePacked(image->GetHostData() ? image->GetHostData() : image->GetData(), image->GetSize(),
                          static_cast<int>(image->GetWidth()), static_cast<int>(image->GetHeight()),
                          image->GetPixelFormat() == media::PixelFormat::PIXEL_BGR8, prompt,
                          input_dump_prefix);
}

nlohmann::json RkllmVlmBackend::EvaluateRgb(const uint8_t* pixels, size_t bytes, int width, int height,
                                            const std::string& prompt, const std::string& input_dump_prefix) {
    return EvaluatePacked(pixels, bytes, width, height, false, prompt, input_dump_prefix);
}

nlohmann::json RkllmVlmBackend::EvaluatePacked(const uint8_t* pixels, size_t bytes, int width, int height,
                                               bool bgr, const std::string& prompt,
                                               const std::string& input_dump_prefix) {
    using Json         = nlohmann::json;
    using Clock        = std::chrono::steady_clock;
    const auto started = Clock::now();
    Json result        = {{"status", "inference_error"},
                          {"raw_output", ""},
                          {"error", nullptr},
                          {"stop_reason", "not_started"},
                          {"sdk_state", nullptr},
                          {"sdk_return_code", nullptr},
                          {"truncated", nullptr},
                          {"input_tokens", nullptr},
                          {"output_tokens", nullptr},
                          {"prefill_ms", nullptr},
                          {"generate_ms", nullptr},
                          {"sdk_memory_mb", nullptr},
                          {"history_clear_code", nullptr},
                          {"running_after", nullptr},
                          {"abort_code", nullptr},
                          {"output_budget_reached", nullptr},
                          {"process_restart_required", false},
                          {"effective_config", GetEvaluationMetadata()}};
    bool submitted     = false;
    try {
        if (!impl_ || !evaluation_enabled_)
            throw std::runtime_error("evaluation backend not initialized");
        if (impl_->evaluation_poisoned) {
            result["process_restart_required"] = true;
            throw std::runtime_error("evaluation process requires restart after previous SDK failure");
        }
        const int running_before = IsEvaluationRunning();
        if (running_before != 0) {
            result["process_restart_required"] = true;
            throw std::runtime_error("SDK not idle before request");
        }
        if (!pixels || prompt.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::invalid_argument("image and nonempty prompt required");
        if (prompt.find("<image>") != std::string::npos ||
            prompt.find("<|image_pad|>") != std::string::npos ||
            prompt.find("<|vision_start|>") != std::string::npos)
            throw std::invalid_argument("prompt must not supply additional image placeholders");
        auto rgb                = vlm_evaluation::ResizeRgb448(pixels, bytes, width, height, bgr);
        result["preprocess_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        const auto prompt_end   = prompt.find_last_not_of(" \t\r\n");
        const std::string sdk_prompt = "<image>" + prompt.substr(0, prompt_end + 1);
        if (!input_dump_prefix.empty()) {
            std::ofstream pixel_file(input_dump_prefix + ".rgb.u8", std::ios::binary);
            pixel_file.write(reinterpret_cast<const char*>(rgb.data()),
                             static_cast<std::streamsize>(rgb.size()));
            pixel_file.close();
            std::ofstream description(input_dump_prefix + ".input.json");
            description << Json({{"sdk_prompt", sdk_prompt},
                                 {"input_token_ids", nullptr},
                                 {"input_shape", {1, 448, 448, 3}},
                                 {"input_type", "uint8"},
                                 {"expected_input_tokens", evaluation_options_.expected_input_tokens},
                                 {"effective_config", GetEvaluationMetadata()}})
                               .dump(2);
            description.close();
            if (!pixel_file || !description)
                throw std::runtime_error("cannot write input evidence");
        }
        const int cleared            = rkllm_clear_kv_cache(impl_->llm, 0, nullptr, nullptr);
        result["history_clear_code"] = cleared;
        if (cleared != 0) {
            result["process_restart_required"] = true;
            throw std::runtime_error("SDK history clear failed");
        }

        const auto vision_started = Clock::now();
        rknn_input vision_input{};
        vision_input.index = 0;
        vision_input.type  = RKNN_TENSOR_UINT8;
        vision_input.fmt   = RKNN_TENSOR_NHWC;
        vision_input.size  = static_cast<uint32_t>(rgb.size());
        vision_input.buf   = rgb.data();
        if (rknn_inputs_set(impl_->encoder.ctx, 1, &vision_input) != RKNN_SUCC ||
            rknn_run(impl_->encoder.ctx, nullptr) != RKNN_SUCC)
            throw std::runtime_error("vision inference failed");
        rknn_output vision_output{};
        vision_output.want_float = 1;
        if (rknn_outputs_get(impl_->encoder.ctx, 1, &vision_output, nullptr) != RKNN_SUCC)
            throw std::runtime_error("vision output retrieval failed");
        std::vector<float> embedding;
        try {
            if (!vision_output.buf || vision_output.size != 196 * 1024 * sizeof(float))
                throw std::runtime_error("vision output byte size does not match 196x1024 float32");
            const auto* values = static_cast<const float*>(vision_output.buf);
            embedding.assign(values, values + 196 * 1024);
        } catch (...) {
            rknn_outputs_release(impl_->encoder.ctx, 1, &vision_output);
            throw;
        }
        if (rknn_outputs_release(impl_->encoder.ctx, 1, &vision_output) != RKNN_SUCC)
            throw std::runtime_error("vision output release failed");
        if (!std::all_of(embedding.begin(), embedding.end(), [](float v) { return std::isfinite(v); }))
            throw std::runtime_error("vision output contains non-finite values");
        result["vision_ms"] =
            std::chrono::duration<double, std::milli>(Clock::now() - vision_started).count();
        if (!input_dump_prefix.empty()) {
            std::ofstream output(input_dump_prefix + ".vision.f32", std::ios::binary);
            output.write(reinterpret_cast<const char*>(embedding.data()),
                         static_cast<std::streamsize>(embedding.size() * sizeof(float)));
            output.close();
            if (!output)
                throw std::runtime_error("cannot write vision embedding evidence");
        }
        RKLLMInput input{};
        input.role                                  = "user";
        input.enable_thinking                       = false;
        input.input_type                            = RKLLM_INPUT_MULTIMODAL;
        input.multimodal_input.prompt               = const_cast<char*>(sdk_prompt.c_str());
        input.multimodal_input.image.image_embed    = embedding.data();
        input.multimodal_input.image.n_image_tokens = 196;
        input.multimodal_input.image.n_image        = 1;
        input.multimodal_input.image.image_start    = "<|vision_start|>";
        input.multimodal_input.image.image_end      = "<|vision_end|>";
        input.multimodal_input.image.image_content  = "<|image_pad|>";
        input.multimodal_input.image.image_width    = 448;
        input.multimodal_input.image.image_height   = 448;
        RKLLMSamplingParam sampling{};
        sampling.top_k          = 1;
        sampling.top_p          = 1.0F;
        sampling.temperature    = 0.0F;
        sampling.repeat_penalty = 1.0F;
        RKLLMInferParam infer{};
        infer.mode                        = RKLLM_INFER_GENERATE;
        infer.keep_history                = 0;
        infer.max_new_tokens              = max_new_tokens_;
        infer.sampling_params             = &sampling;
        impl_->evaluation_run             = std::make_unique<RunContext>();
        impl_->evaluation_run->evaluation = true;
        submitted                         = true;
        // Keep every SDK argument alive even if a failed synchronous call reports
        // that work is still running; the supervisor must then replace the process.
        impl_->evaluation_prompt                                   = sdk_prompt;
        impl_->evaluation_embedding                                = std::move(embedding);
        impl_->evaluation_input                                    = input;
        impl_->evaluation_input.multimodal_input.prompt            = impl_->evaluation_prompt.data();
        impl_->evaluation_input.multimodal_input.image.image_embed = impl_->evaluation_embedding.data();
        impl_->evaluation_sampling                                 = sampling;
        impl_->evaluation_infer                                    = infer;
        impl_->evaluation_infer.sampling_params                    = &impl_->evaluation_sampling;
        const int ret             = rkllm_run(impl_->llm, &impl_->evaluation_input, &impl_->evaluation_infer,
                                              impl_->evaluation_run.get());
        result["sdk_return_code"] = ret;
        int running_after         = IsEvaluationRunning();
        if (running_after != 0) {
            result["abort_code"]               = AbortEvaluation();
            running_after                      = IsEvaluationRunning();
            result["process_restart_required"] = true;
        }
        result["running_after"] = running_after;
        const auto& run         = *impl_->evaluation_run;
        std::lock_guard<std::mutex> lock(impl_->evaluation_run->mutex);
        result["raw_output"]             = run.text;
        result["sdk_state"]              = run.last_state;
        result["output_token_ids"]       = run.token_ids;
        result["callback_output_tokens"] = run.token_ids.size();
        result["output_budget_reached"] =
            vlm_evaluation::OutputBudgetReached(run.token_ids.size(), max_new_tokens_);
        result["sdk_states"]  = run.states;
        result["stop_reason"] = run.finished ? "sdk_finish" : "sdk_error";
        if (run.has_perf) {
            result["input_tokens"]  = run.perf.prefill_tokens;
            result["output_tokens"] = run.perf.generate_tokens;
            result["prefill_ms"]    = run.perf.prefill_time_ms;
            result["generate_ms"]   = run.perf.generate_time_ms;
            result["sdk_memory_mb"] = run.perf.memory_usage_mb;
        }
        if (ret != 0 || run.failed || !run.finished || running_after != 0)
            throw std::runtime_error("SDK inference did not complete successfully and idle");
        result["input_token_reference_match"] =
            run.has_perf
                ? nlohmann::json(run.perf.prefill_tokens == evaluation_options_.expected_input_tokens)
                : nlohmann::json(nullptr);
        if (!run.has_perf ||
            !vlm_evaluation::CallbackOutputWithinBudget(run.token_ids.size(), max_new_tokens_) ||
            !vlm_evaluation::TokenCountsWithinBudget(run.perf.prefill_tokens, run.perf.generate_tokens,
                                                     context_length_, max_new_tokens_))
            throw std::invalid_argument(
                "actual SDK token counts missing, invalid or exceed context/output budget");
        result["status"] = run.text.empty() ? "empty_output" : "ok";
    } catch (const std::invalid_argument& error) {
        result["status"] = "validation_error";
        result["error"]  = error.what();
        if (!submitted)
            result["stop_reason"] = "validation_error";
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    if (impl_ && result["process_restart_required"].get<bool>())
        impl_->evaluation_poisoned = true;
    result["inference_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return result;
}

util::ErrorEnum RkllmVlmBackend::Generate(const std::vector<VideoFramePtr>& images,
                                          const std::vector<std::string>& prompts,
                                          const Qwen3VLGenerationParam& gen_param,
                                          std::vector<Qwen3VLResult>& results) {
    if (!impl_) {
        return util::ErrorEnum::NotInit;
    }
    if (images.size() != prompts.size()) {
        return util::ErrorEnum::InvalidParam;
    }

    // The worker repeatedly calls Generate on the same thread. Keep the two large
    // scratch buffers thread-local so they retain capacity without sharing mutable
    // memory between camera workers.
    thread_local std::vector<uint8_t> rgb;
    thread_local std::vector<float> embedding;
    for (size_t i = 0; i < images.size(); ++i) {
        if (!images[i] || !VideoFrameValid(images[i])) {
            return util::ErrorEnum::InvalidParam;
        }
        const auto preprocess_start = std::chrono::steady_clock::now();
        if (!ResizeFrameToRgb(images[i], impl_->encoder.width, impl_->encoder.height, rgb)) {
            LOG_ERRO("RKLLM unsupported or empty frame. format:{} dims:{}x{}",
                     static_cast<int>(images[i]->GetPixelFormat()), images[i]->GetWidth(),
                     images[i]->GetHeight());
            return util::ErrorEnum::InvalidParam;
        }
        const auto vision_start = std::chrono::steady_clock::now();
        if (!EncodeImage(impl_->encoder, rgb, embedding)) {
            return util::ErrorEnum::AI_FORWARD_FAILED;
        }
        const auto prefill_start = std::chrono::steady_clock::now();

        std::string prompt = prompts[i];
        if (prompt.find("<image>") == std::string::npos) {
            prompt.insert(0, "<image>");
        }
        RKLLMInput input{};
        input.role                                  = "user";
        input.enable_thinking                       = false;
        input.input_type                            = RKLLM_INPUT_MULTIMODAL;
        input.multimodal_input.prompt               = prompt.data();
        input.multimodal_input.image.image_embed    = embedding.data();
        input.multimodal_input.image.n_image_tokens = impl_->encoder.image_tokens;
        input.multimodal_input.image.n_image        = 1;
        input.multimodal_input.image.image_start    = "<|vision_start|>";
        input.multimodal_input.image.image_end      = "<|vision_end|>";
        input.multimodal_input.image.image_content  = "<|image_pad|>";
        input.multimodal_input.image.image_width    = impl_->encoder.width;
        input.multimodal_input.image.image_height   = impl_->encoder.height;

        RKLLMSamplingParam sampling{};
        sampling.top_k          = gen_param.do_sample ? gen_param.top_k : 1;
        sampling.top_p          = gen_param.do_sample ? gen_param.top_p : 1.0F;
        sampling.temperature    = gen_param.do_sample ? gen_param.temperature : 0.0F;
        sampling.repeat_penalty = 1.1F;
        RKLLMInferParam infer{};
        infer.mode            = RKLLM_INFER_GENERATE;
        infer.keep_history    = 0;
        infer.max_new_tokens  = max_new_tokens_;
        infer.sampling_params = &sampling;
        RunContext run;
        run.text.reserve(8);
        const int ret = rkllm_run(impl_->llm, &input, &infer, &run);
        if (ret != 0 || run.failed) {
            LOG_ERRO("RKLLM multimodal inference failed. ret:{} callbackError:{}", ret, run.failed);
            return util::ErrorEnum::AI_FORWARD_FAILED;
        }

        Qwen3VLResult result;
        result.text        = std::move(run.text);
        result.frame_index = static_cast<int64_t>(images[i]->GetFrameIndex());
        result.timestamp   = images[i]->GetTimestamp();
        const auto finish  = std::chrono::steady_clock::now();
        const auto preprocess_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(vision_start - preprocess_start).count();
        const auto vision_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(prefill_start - vision_start).count();
        const auto llm_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(finish - prefill_start).count();
        LOG_INFO(
            "[Qwen3VL][RKLLM][Timing] preprocess:{}ms vision:{}ms llm:{}ms total:{}ms "
            "tokens:{}",
            preprocess_ms, vision_ms, llm_ms, preprocess_ms + vision_ms + llm_ms,
            impl_->encoder.image_tokens);
        LOG_INFO("[Qwen3VL][RKLLM] frameIndex:{} result:{}", result.frame_index, result.text);
        results.push_back(std::move(result));
    }
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
