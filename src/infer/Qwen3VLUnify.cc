// Qwen3VLUnify.cc — Qwen3 vision-language model implementation.

#include "infer/Qwen3VLUnify.h"

#include <algorithm>
#include <cstring>

#include "infer/AiComponment.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {
Qwen3VLUnify::Qwen3VLUnify(const std::string& atomic_code, const std::string& json_path,
                           const std::string& model_path, const std::string& tokenizer_path)
    : atomic_code_(atomic_code),
      cfg_path_(json_path),
      model_path_(model_path),
      tokenizer_path_(tokenizer_path) {}

Qwen3VLUnify::~Qwen3VLUnify() {
    LOG_INFO("{}", "Qwen3VLUnify Delete");
}

util::ErrorEnum Qwen3VLUnify::Init() {
    if (generator_) {
        LOG_WARN("{}", "Init SDK Qwen3VL Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    try {
        // Pass tokenizer_path as the tokenizer_path parameter
        generator_ = std::make_unique<cosmo::nn::DefaultComponent>(cfg_path_, model_path_, GetDeviceType(),
                                                                   &profiler_, tokenizer_path_);
    } catch (const std::exception& e) {
        LOG_ERRO("Init SDK Qwen3VL Failed. CfgPath:{} ModelPath:{} TokenizerPath:{}, {}", cfg_path_,
                 model_path_, tokenizer_path_, e.what());
        generator_.reset();
        return util::ErrorEnum::Failed;
    }

    if (!generator_) {
        LOG_ERRO("Init SDK Qwen3VL Failed. CfgPath:{} ModelPath:{} TokenizerPath:{}", cfg_path_, model_path_,
                 tokenizer_path_);
        return util::ErrorEnum::Failed;
    }
    LOG_INFO("Init SDK Qwen3VL Ok. CfgPath:{} ModelPath:{} TokenizerPath:{}", cfg_path_, model_path_,
             tokenizer_path_);

    max_batch_size_ = static_cast<size_t>(generator_->GetMaxBatchSize());

    return util::ErrorEnum::Success;
}

util::ErrorEnum Qwen3VLUnify::Generate(const std::vector<VideoFramePtr>& images,
                                       const std::vector<std::string>& prompts,
                                       const Qwen3VLGenerationParam& gen_param,
                                       std::vector<Qwen3VLResult>& results) {
    auto start = std::chrono::high_resolution_clock::now();
    if (!generator_) {
        LOG_WARN("{}", "SDK Qwen3VL Not Init");
        return util::ErrorEnum::NotInit;
    }

    if (images.size() != prompts.size()) {
        LOG_ERRO("Qwen3VL images size({}) != prompts size({})", images.size(), prompts.size());
        return util::ErrorEnum::InvalidParam;
    }

    for (size_t i = 0; i < prompts.size(); i++) {
        LOG_DEBUG("[Qwen3VL] Generate input prompt[{}]: {}", i, prompts[i]);
    }
    LOG_DEBUG("[Qwen3VL] Generate param do_sample:{} top_k:{} top_p:{} temperature:{} imageNum:{}",
             gen_param.do_sample, gen_param.top_k, gen_param.top_p, gen_param.temperature, images.size());

    size_t image_num{images.size()};
    std::vector<VideoFramePtr> inputs;
    std::vector<std::string> input_prompts;

    for (size_t i = 0; i < image_num; i++) {
        inputs.push_back(images[i]);
        input_prompts.push_back(prompts[i]);
        size_t input_size{inputs.size()};
        if (input_size == max_batch_size_ || (i + 1) == image_num) {
            std::vector<Qwen3VLResult> outputs;
            auto ret = Forward(inputs, input_prompts, gen_param, outputs);
            if (util::ErrorEnum::Success != ret) {
                LOG_ERRO("Qwen3VL Forward Failed. Ret:{}", ret);
                return ret;
            }
            std::copy(outputs.begin(), outputs.end(), std::back_inserter(results));
            inputs.clear();
            input_prompts.clear();
        }
    }
    for (size_t i = 0; i < results.size(); i++) {
        LOG_DEBUG("[Qwen3VL] Generate output result[{}] frameIndex:{} text: {}", i, results[i].frame_index,
                 results[i].text);
    }
    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    LOG_DEBUG("Qwen3VL Generate execution time: {} ms, imageNum: {}", duration.count(), image_num);
    return util::ErrorEnum::Success;
}

util::ErrorEnum Qwen3VLUnify::Forward(const std::vector<VideoFramePtr>& images,
                                      const std::vector<std::string>& prompts,
                                      const Qwen3VLGenerationParam& /*gen_param*/,
                                      std::vector<Qwen3VLResult>& results) {
    // Qwen3VL uses host-memory BGR blobs (VideoFrame is decoded host memory)
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    for (const auto& img : images) {
        if (!img || !VideoFrameValid(img)) {
            LOG_ERRO("{}", "Qwen3VL Invalid image");
            return util::ErrorEnum::InvalidParam;
        }
        // Qwen3VL needs a contiguous host BGR buffer; prefer hostData, fallback to GetData
        auto base_ptr = img->GetHostData() ? reinterpret_cast<void*>(img->GetHostData())
                                           : reinterpret_cast<void*>(img->GetData());
        if (!base_ptr) {
            LOG_ERRO("{}", "Qwen3VL frame has no data");
            return util::ErrorEnum::Failed;
        }
        int h = static_cast<int>(img->GetHeight());
        int w = static_cast<int>(img->GetWidth());
        if (h <= 0 || w <= 0) {
            LOG_ERRO("Qwen3VL Invalid dimensions {}x{}", w, h);
            return util::ErrorEnum::InvalidParam;
        }
        LOG_INFO(
            "[Qwen3VL] Forward input imageIdx:{} frameIndex:{} timestamp:{} dims:{}x{} hostData:{} data:{}",
            image_blobs.size(), img->GetFrameIndex(), img->GetTimestamp(), w, h, img->GetHostData() ? 1 : 0,
            img->GetData() ? 1 : 0);
        cosmo::nn::BlobDesc desc;
        desc.image_format = cosmo::nn::ImageFormat::IMAGE_BGR;
        desc.data_format  = cosmo::nn::DataFormat::DATA_FORMAT_NHWC;
        desc.data_type    = cosmo::nn::DataType::DATA_TYPE_UINT8;
        desc.dims         = {1, h, w, 3};
        desc.device_type  = cosmo::nn::DeviceType::DEVICE_NAIVE;
        cosmo::nn::BlobHandle handle;
        handle.base = base_ptr;
        image_blobs.push_back(std::make_shared<cosmo::nn::Blob>(desc, handle));
    }

    // Ref sophon_simple_graph.cc: Qwen3VL requires Forward({images, prompts})
    // Prompt is a host Blob of UTF-8 text bytes, dims={1, size}
    std::vector<std::shared_ptr<cosmo::nn::Blob>> prompt_blobs;
    std::vector<std::vector<char>> prompt_data_storage;
    for (const auto& prompt : prompts) {
        prompt_data_storage.emplace_back(prompt.begin(), prompt.end());
        cosmo::nn::BlobDesc desc;
        desc.data_type   = cosmo::nn::DataType::DATA_TYPE_UINT8;
        desc.dims        = {1, static_cast<int>(prompt_data_storage.back().size())};
        desc.device_type = cosmo::nn::DeviceType::DEVICE_NAIVE;
        cosmo::nn::BlobHandle handle;
        handle.base = prompt_data_storage.back().empty() ? nullptr : prompt_data_storage.back().data();
        prompt_blobs.push_back(std::make_shared<cosmo::nn::Blob>(desc, handle));
    }

    auto status = generator_->Forward({image_blobs, prompt_blobs});
    if (!bool(status)) {
        LOG_ERRO("Qwen3VL Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    // Qwen3VL output: demo (sophon_simple_graph) uses ParseOutput<std::string>.
    // Prefer string parsing; if current CWNN version doesn't support it, fallback to byte output.
    std::vector<std::vector<std::string>> texts;
    status = generator_->ParseOutput<std::string>(texts);
    if (bool(status)) {
        // CWNN ParseOutput shape is [batch][...]
        for (size_t i = 0; i < texts.size() && i < images.size(); i++) {
            Qwen3VLResult result;
            if (!texts.at(i).empty())
                result.text = texts.at(i).at(0);
            result.frame_index = static_cast<int64_t>(images.at(i)->GetFrameIndex());
            result.timestamp   = images.at(i)->GetTimestamp();
            results.push_back(result);
        }
    } else {
        LOG_WARN("Qwen3VL ParseOutput<string> Failed.({}), fallback to bytes", status.description());
        std::vector<std::vector<uint8_t>> outputs;
        status = generator_->ParseOutput<uint8_t>(outputs);
        if (!bool(status)) {
            LOG_ERRO("Qwen3VL ParseOutput Failed.({})", status.description());
            return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
        }

        for (size_t i = 0; i < outputs.size() && i < images.size(); i++) {
            Qwen3VLResult result;
            if (!outputs.at(i).empty()) {
                result.text.assign(reinterpret_cast<const char*>(outputs.at(i).data()), outputs.at(i).size());
            }
            result.frame_index = static_cast<int64_t>(images.at(i)->GetFrameIndex());
            result.timestamp   = images.at(i)->GetTimestamp();
            results.push_back(result);
        }
    }
    // Inference result log: verify invocation and generated content
    for (size_t i = 0; i < results.size(); i++) {
        std::string preview = results[i].text.empty() ? "(empty)" : results[i].text;
        if (preview.length() > 200)
            preview = preview.substr(0, 200) + "...";
        LOG_DEBUG("[Qwen3VL] Forward done. imageIdx:{} frameIndex:{} text:{}", i, results[i].frame_index,
                 preview);
    }
    LOG_DEBUG("[Qwen3VL] Forward done. images:{} results:{}", images.size(), results.size());
    return util::ErrorEnum::Success;
}

util::ErrorEnum Qwen3VLUnify::GetMaxBatchSize(size_t* value) const {
    if (!generator_) {
        LOG_WARN("{}", "SDK Qwen3VL Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (value) {
        *value = static_cast<size_t>(generator_->GetMaxBatchSize());
    }
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
