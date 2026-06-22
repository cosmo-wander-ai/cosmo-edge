// AiDetectorUnify — Unified detection engine wrapper.

#include "infer/AiDetectorUnify.h"

#include <algorithm>
#include <exception>
#include <iterator>

#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {
AiDetectorUnify::AiDetectorUnify(const std::string& atomic_code, const std::string& json_path,
                                 const std::string& model_path)
    : atomic_code_(atomic_code), cfg_path_(json_path), model_path_(model_path) {}

AiDetectorUnify::~AiDetectorUnify() {
    LOG_INFO("{}", "AiDetectorUnify Delete");
}

util::ErrorEnum AiDetectorUnify::Init() {
    if (detector_) {
        LOG_WARN("{}", "Init SDK Detector Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    try {
        detector_ = std::make_unique<cosmo::nn::DefaultComponent>(cfg_path_, model_path_, GetDeviceType(),
                                                                  &profiler_);
    } catch (const std::exception& e) {
        LOG_ERRO("Init SDK Detector Failed. CfgPath:{} ModelPath:{}, {}", cfg_path_, model_path_, e.what());
        detector_.reset();
        return util::ErrorEnum::Failed;
    }

    if (!detector_) {
        LOG_ERRO("Init SDK Detector Failed. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);
        return util::ErrorEnum::Failed;
    }
    LOG_INFO("Init SDK Detector Ok. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);

    max_batch_size_ = static_cast<size_t>(detector_->GetMaxBatchSize());
    LOG_DEBUG("{} Detector Max Batch Size {}", model_path_, max_batch_size_);

    labels_ = detector_->GetSelectedClassnames();

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiDetectorUnify::Detect(const std::vector<VideoFramePtr>& images,
                                        std::vector<AiConfidence> conf_thres,
                                        std::vector<std::vector<AiDetectRstEl>>& results) {
    if (!detector_) {
        LOG_WARN("{}", "SDK Detector Not Init");
        return util::ErrorEnum::NotInit;
    }

    std::for_each(conf_thres.begin(), conf_thres.end(),
                  [this](const auto& conf) { detector_->SetThreshold(conf.label, conf.confidence); });

    try {
        size_t image_num = images.size();
        std::vector<VideoFramePtr> inputs;
        for (size_t i = 0; i < image_num; i++) {
            inputs.push_back(images[i]);
            size_t input_size = inputs.size();
            if (input_size == max_batch_size_ || (i + 1) == image_num) {
                std::vector<std::vector<AiDetectRstEl>> outputs;
                auto ret = Forward(inputs, outputs);
                if (util::ErrorEnum::Success != ret) {
                    LOG_ERRO("Forward Failed. Ret:{}", ret);
                    return ret;
                }
                if (outputs.size() < 1) {
                    std::vector<AiDetectRstEl> emptyEl;
                    for (size_t j = 0; j < input_size; j++) {
                        results.push_back(emptyEl);
                    }
                } else {
                    std::copy(outputs.begin(), outputs.end(), std::back_inserter(results));
                }
                inputs.clear();
            }
        }
    } catch (const std::exception& e) {
        LOG_ERRO("Detect Exception. CfgPath:{} ModelPath:{} images:{} confThres:{} maxBatch:{}, {}",
                 cfg_path_, model_path_, images.size(), conf_thres.size(), max_batch_size_, e.what());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    } catch (...) {
        LOG_ERRO("Detect non-std exception. CfgPath:{} ModelPath:{} images:{} confThres:{} maxBatch:{}",
                 cfg_path_, model_path_, images.size(), conf_thres.size(), max_batch_size_);
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiDetectorUnify::Forward(const std::vector<VideoFramePtr>& images,
                                         std::vector<std::vector<AiDetectRstEl>>& results) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }
    try {
        auto status = detector_->Forward({image_blobs});
        if (!bool(status)) {
            LOG_ERRO("Forward Failed.({})", status.description());
            return util::ErrorEnum::AI_FORWARD_FAILED;
        }
    } catch (const std::exception& e) {
        LOG_ERRO("Forward Exception. CfgPath:{} ModelPath:{} inputBatch:{}, {}", cfg_path_, model_path_,
                 image_blobs.size(), e.what());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    } catch (...) {
        LOG_ERRO("Forward non-std exception. CfgPath:{} ModelPath:{} inputBatch:{}", cfg_path_, model_path_,
                 image_blobs.size());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
    try {
        auto status = detector_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
        if (!bool(status)) {
            LOG_ERRO("ParseOutput Failed.({})", status.description());
            return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
        }
    } catch (const std::exception& e) {
        LOG_ERRO("ParseOutput Exception. CfgPath:{} ModelPath:{}, {}", cfg_path_, model_path_, e.what());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    } catch (...) {
        LOG_ERRO("ParseOutput non-std exception. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }
    if (outputs.size() > image_blobs.size()) {
        LOG_WARN(
            "ParseOutput size:{} larger than inputBatch:{}, extra outputs ignored. CfgPath:{} ModelPath:{}",
            outputs.size(), image_blobs.size(), cfg_path_, model_path_);
    }
    for (size_t i = 0; i < outputs.size() && i < image_blobs.size(); i++) {
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
            el.hwRatio = static_cast<float>(el.box.height) / static_cast<float>(el.box.width);
            if (obj.infos.empty()) {
                continue;
            }
            el.confidence.confidence  = obj.infos.at(0).confidence;
            el.confidence.label       = obj.infos.at(0).class_name;
            el.confidence.atomic_code = atomic_code_;
            el.classId                = obj.infos.at(0).class_id + 1;
            el.targetId               = util::GenerateUUID();
            result.push_back(el);
        }
        results.push_back(result);
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiDetectorUnify::GetMaxBatchSize(size_t& value) {
    if (!detector_) {
        LOG_WARN("{}", "SDK Detector Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(detector_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
