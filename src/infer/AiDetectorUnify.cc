// AiDetectorUnify — Unified detection engine wrapper.

#include "infer/AiDetectorUnify.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iterator>
#include <utility>

#include "nn/core/inference_pipeline_metrics.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {
namespace {
    using MetricsClock = std::chrono::steady_clock;

    uint64_t ElapsedNanoseconds(MetricsClock::time_point started_at) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(MetricsClock::now() - started_at).count());
    }
}  // namespace

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
        cosmo::nn::DefaultComponent::Options options;
        options.profiler = &profiler_;
        detector_ =
            std::make_unique<cosmo::nn::DefaultComponent>(options, cfg_path_, model_path_, GetDeviceType());
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
    return Detect(images, {}, std::move(conf_thres), results);
}

util::ErrorEnum AiDetectorUnify::Detect(const std::vector<VideoFramePtr>& images,
                                        const std::vector<media::NativeVideoBufferPtr>& native_buffers,
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
        std::vector<media::NativeVideoBufferPtr> native_inputs;
        for (size_t i = 0; i < image_num; i++) {
            inputs.push_back(images[i]);
            native_inputs.push_back(i < native_buffers.size() ? native_buffers[i] : nullptr);
            size_t input_size = inputs.size();
            if (input_size == max_batch_size_ || (i + 1) == image_num) {
                std::vector<std::vector<AiDetectRstEl>> outputs;
                auto ret = Forward(inputs, native_inputs, outputs);
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
                native_inputs.clear();
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
                                         const std::vector<media::NativeVideoBufferPtr>& native_buffers,
                                         std::vector<std::vector<AiDetectRstEl>>& results) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    const auto blob_convert_started = MetricsClock::now();
    auto ret                        = ConvertImagesToBlobs(images, native_buffers, image_blobs);
    cosmo::nn::GetInferencePipelineMetrics().RecordBlobConvert(ElapsedNanoseconds(blob_convert_started),
                                                               static_cast<uint64_t>(image_blobs.size()));
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }
    const auto graph_forward_started = MetricsClock::now();
    try {
        auto status = detector_->Forward({image_blobs});
        if (!bool(status)) {
            cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
                ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), false);
            LOG_ERRO("Forward Failed.({})", status.description());
            return util::ErrorEnum::AI_FORWARD_FAILED;
        }
    } catch (const std::exception& e) {
        cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
            ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("Forward Exception. CfgPath:{} ModelPath:{} inputBatch:{}, {}", cfg_path_, model_path_,
                 image_blobs.size(), e.what());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    } catch (...) {
        cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
            ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("Forward non-std exception. CfgPath:{} ModelPath:{} inputBatch:{}", cfg_path_, model_path_,
                 image_blobs.size());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    cosmo::nn::GetInferencePipelineMetrics().RecordGraphForward(
        ElapsedNanoseconds(graph_forward_started), static_cast<uint64_t>(image_blobs.size()), true);

    std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
    const auto result_parse_started = MetricsClock::now();
    try {
        auto status = detector_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
        if (!bool(status)) {
            cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
                ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), false);
            LOG_ERRO("ParseOutput Failed.({})", status.description());
            return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
        }
    } catch (const std::exception& e) {
        cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
            ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("ParseOutput Exception. CfgPath:{} ModelPath:{}, {}", cfg_path_, model_path_, e.what());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    } catch (...) {
        cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
            ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), false);
        LOG_ERRO("ParseOutput non-std exception. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }
    cosmo::nn::GetInferencePipelineMetrics().RecordResultParse(
        ElapsedNanoseconds(result_parse_started), static_cast<uint64_t>(image_blobs.size()), true);
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
            if (obj.oriented_corners) {
                const double max_x = dims.at(2) - 1.0;
                const double max_y = dims.at(1) - 1.0;
                if (max_x < 0 || max_y < 0 || obj.x2 < 0 || obj.y2 < 0 || obj.x1 > max_x || obj.y1 > max_y ||
                    !std::isfinite(obj.x1) || !std::isfinite(obj.y1) || !std::isfinite(obj.x2) ||
                    !std::isfinite(obj.y2))
                    continue;
                // Bound floating-point coordinates before converting to integer pixels.
                el.box.x = static_cast<int>(std::floor(std::clamp<double>(obj.x1, 0, max_x)));
                el.box.y = static_cast<int>(std::floor(std::clamp<double>(obj.y1, 0, max_y)));
                el.box.width =
                    static_cast<int>(std::ceil(std::clamp<double>(obj.x2, 0, max_x))) - el.box.x + 1;
                el.box.height =
                    static_cast<int>(std::ceil(std::clamp<double>(obj.y2, 0, max_y))) - el.box.y + 1;
            } else {
                el.box.x      = static_cast<int>(obj.x1);
                el.box.y      = static_cast<int>(obj.y1);
                el.box.width  = static_cast<int>(obj.x2) - static_cast<int>(obj.x1) + 1;
                el.box.height = static_cast<int>(obj.y2) - static_cast<int>(obj.y1) + 1;
                el.box &= util::Box(0, 0, dims.at(2) - 1, dims.at(1) - 1);
            }
            el.oriented_corners = obj.oriented_corners;
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
