#include "infer/RkllmVlmBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>

#include "infer/Qwen3VLUnify.h"
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
    };

    int RkllmResultCallback(RKLLMResult* result, void* userdata, LLMCallState state) {
        auto* context = static_cast<RunContext*>(userdata);
        if (!context) {
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
    // This backend is used for short visual judgements. 64 image tokens plus the
    // prompt and answer fit comfortably in 512 tokens, avoiding an oversized KV
    // cache and generation budget on the edge device.
    param.max_new_tokens                = 2;
    param.max_context_len               = 512;
    param.skip_special_token            = true;
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

    impl_ = candidate.release();
    LOG_INFO("RKLLM multimodal backend initialized. llm:{} vision:{}", model_path_, vision_path.string());
    return util::ErrorEnum::Success;
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
        infer.max_new_tokens  = 2;
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
