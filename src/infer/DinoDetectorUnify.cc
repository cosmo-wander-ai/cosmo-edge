// DinoDetectorUnify.cc — DINO visual detection model implementation.

#include "infer/DinoDetectorUnify.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numeric>
#include <sstream>

#include "infer/AiComponment.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {
DinoDetectorUnify::DinoDetectorUnify(const std::string& atomicCode, const std::string& jsonPath,
                                     const std::string& modelPath, const std::string& vocabPath)
    : atomic_code_(atomicCode), cfg_path_(jsonPath), model_path_(modelPath), vocab_path_(vocabPath) {}

DinoDetectorUnify::~DinoDetectorUnify() {
    LOG_INFO("{}", "DinoDetectorUnify Delete");
}

util::ErrorEnum DinoDetectorUnify::Init() {
    if (detector_) {
        LOG_WARN("{}", "Init SDK Dino Detector Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    try {
        // Pass vocabPath as the tokenizer_path parameter
        detector_ = std::make_unique<cosmo::nn::DefaultComponent>(cfg_path_, model_path_, GetDeviceType(),
                                                                  &profiler_, vocab_path_);
    } catch (const std::exception& e) {
        LOG_ERRO("Init SDK Dino Detector Failed. CfgPath:{} ModelPath:{} VocabPath:{}, {}", cfg_path_,
                 model_path_, vocab_path_, e.what());
        detector_.reset();
        return util::ErrorEnum::Failed;
    }

    if (!detector_) {
        LOG_ERRO("Init SDK Dino Detector Failed. CfgPath:{} ModelPath:{} VocabPath:{}", cfg_path_,
                 model_path_, vocab_path_);
        return util::ErrorEnum::Failed;
    }
    LOG_INFO("Init SDK Dino Detector Ok. CfgPath:{} ModelPath:{} VocabPath:{}", cfg_path_, model_path_,
             vocab_path_);

    max_batch_size_ = static_cast<size_t>(detector_->GetMaxBatchSize());

    return util::ErrorEnum::Success;
}

std::vector<std::string> DinoDetectorUnify::GetLabels() {
    if (detector_ && labels_.empty()) {
        labels_ = detector_->GetSelectedClassnames();
    }
    return labels_;
}

util::ErrorEnum DinoDetectorUnify::Detect(const std::vector<VideoFramePtr>& images, const std::string& prompt,
                                          std::vector<std::vector<AiDetectRstEl>>& results) {
    auto start = std::chrono::high_resolution_clock::now();
    if (!detector_) {
        LOG_WARN("{}", "SDK Dino Detector Not Init");
        return util::ErrorEnum::NotInit;
    }

    size_t image_num = images.size();
    std::vector<VideoFramePtr> inputs;
    for (size_t i = 0; i < image_num; i++) {
        inputs.push_back(images[i]);
        size_t input_size = inputs.size();
        if (input_size == max_batch_size_ || (i + 1) == image_num) {
            std::vector<std::vector<AiDetectRstEl>> outputs;
            auto ret = Forward(inputs, prompt, outputs);
            if (util::ErrorEnum::Success != ret) {
                LOG_ERRO("Dino Forward Failed. Ret:{}", ret);
                return ret;
            }
            if (outputs.size() < 1) {
                std::vector<AiDetectRstEl> empty_el;
                results.insert(results.end(), input_size, empty_el);
            } else {
                std::copy(outputs.begin(), outputs.end(), std::back_inserter(results));
            }
            inputs.clear();
        }
    }
    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    LOG_DEBUG("Dino Detect execution time: {} ms, imageNum: {}", duration.count(), image_num);
    return util::ErrorEnum::Success;
}

util::ErrorEnum DinoDetectorUnify::Forward(const std::vector<VideoFramePtr>& images,
                                           const std::string& prompt,
                                           std::vector<std::vector<AiDetectRstEl>>& results) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    // Consistent with sophon_simple_graph.cc LoadPrompt: input_1 is raw UTF-8 text bytes (UINT8), not token
    // IDs
    std::string prompt_text = prompt.empty() ? "person" : prompt;
    LOG_DEBUG("Dino Forward Prompt:{}", prompt_text);
    std::vector<std::shared_ptr<cosmo::nn::Blob>> prompt_blobs;
    for (size_t i = 0; i < image_blobs.size(); i++) {
        cosmo::nn::BlobDesc p_desc;
        p_desc.data_type   = cosmo::nn::DataType::DATA_TYPE_UINT8;
        p_desc.dims        = {1, static_cast<int>(prompt_text.size())};
        p_desc.device_type = cosmo::nn::DeviceType::DEVICE_NAIVE;
        auto p_blob        = std::make_shared<cosmo::nn::Blob>(p_desc, true);
        void* base         = p_blob->GetHandle().base;
        if (base && !prompt_text.empty())
            memcpy(base, prompt_text.c_str(), prompt_text.size());
        prompt_blobs.push_back(p_blob);
    }

    // Input log: useful for comparing with demo output during debugging
    for (size_t i = 0; i < image_blobs.size(); i++) {
        auto& d = image_blobs[i]->GetBlobDesc();
        std::string dim_str;
        for (size_t k = 0; k < d.dims.size(); k++) {
            if (k)
                dim_str += ",";
            dim_str += std::to_string(d.dims[k]);
        }
        LOG_DEBUG("[DinoDetect] input imageIdx:{} blob dims:[{}] dtype:{} device:{}", i, dim_str,
                 static_cast<int>(d.data_type), static_cast<int>(d.device_type));
    }
    LOG_DEBUG("[DinoDetect] input prompt: \"{}\" len:{} promptBlobs:{} eachDims:[1,{}]", prompt_text,
             prompt_text.size(), prompt_blobs.size(), prompt_text.size());

    auto status = detector_->Forward({image_blobs, prompt_blobs});
    if (!bool(status)) {
        LOG_ERRO("Dino Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
    status = detector_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
    if (!bool(status)) {
        LOG_ERRO("Dino ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    for (size_t i = 0; i < outputs.size(); i++) {
        auto detects = outputs.at(i);
        auto desc    = image_blobs.at(i)->GetBlobDesc();
        auto dims    = desc.dims;
        std::vector<AiDetectRstEl> result;
        for (auto obj : detects) {
            AiDetectRstEl el;
            el.box.x      = static_cast<int>(obj.x1);
            el.box.y      = static_cast<int>(obj.y1);
            el.box.width  = static_cast<int>(obj.x2) - static_cast<int>(obj.x1) + 1;
            el.box.height = static_cast<int>(obj.y2) - static_cast<int>(obj.y1) + 1;
            el.box &= util::Box(0, 0, dims.at(2) - 1, dims.at(1) - 1);
            if (el.box.width <= 0 || el.box.height <= 0) {
                continue;
            }
            el.hwRatio                = static_cast<float>(el.box.height) / static_cast<float>(el.box.width);
            el.confidence.confidence  = obj.infos.at(0).confidence;
            el.confidence.label       = obj.infos.at(0).class_name;
            el.confidence.atomic_code = atomic_code_;
            el.classId                = obj.infos.at(0).class_id + 1;
            el.targetId               = util::GenerateUUID();
            result.push_back(el);
        }
        results.push_back(result);
    }
    // Inference result log: verify invocation and detection count
    size_t total_det = std::accumulate(results.begin(), results.end(), size_t{0},
                                       [](size_t sum, const auto& v) { return sum + v.size(); });
    for (size_t i = 0; i < results.size(); i++) {
        for (size_t j = 0; j < results[i].size() && j < 5; j++) {
            const auto& el = results[i][j];
            LOG_DEBUG("[DinoDetect] imageIdx:{} detIdx:{} box:[{},{},{},{}] label:{} conf:{}", i, j, el.box.x,
                     el.box.y, el.box.width, el.box.height, el.confidence.label, el.confidence.confidence);
        }
        if (results[i].size() > 5)
            LOG_DEBUG("[DinoDetect] imageIdx:{} total:{} (only first 5 logged)", i, results[i].size());
    }
    LOG_DEBUG("[DinoDetect] Forward done. images:{} totalDetections:{}", results.size(), total_det);
    return util::ErrorEnum::Success;
}

util::ErrorEnum DinoDetectorUnify::GetMaxBatchSize(size_t& value) {
    if (!detector_) {
        LOG_WARN("{}", "SDK Dino Detector Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(detector_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
