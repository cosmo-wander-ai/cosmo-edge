// AiClassifierUnify — Ai Classifier Unify implementation.

#include "infer/AiClassifierUnify.h"

#include <algorithm>
#include <iterator>
#include <numeric>

#include "util/Log.h"

namespace cosmo {
AiClassifierUnify::AiClassifierUnify(const std::string& atomic_code, const std::string& json_path,
                                     const std::string& model_path)
    : atomic_code_(atomic_code), cfg_path_(json_path), model_path_(model_path) {}

AiClassifierUnify::~AiClassifierUnify() {
    LOG_INFO("{}", "AiClassifierUnify Delete");
}

util::ErrorEnum AiClassifierUnify::Init() {
    if (classifier_) {
        LOG_WARN("{}", "Init SDK Classifier Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    classifier_ =
        std::make_unique<cosmo::nn::DefaultComponent>(cfg_path_, model_path_, GetDeviceType(), &profiler_);
    LOG_DEBUG("DEBUG: Classifier {} Init", model_path_);

    max_batch_size_ = static_cast<size_t>(classifier_->GetMaxBatchSize());
    LOG_DEBUG("DEBUG: {} Classifier Max Batch Size {}", model_path_, max_batch_size_);

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiClassifierUnify::Classify(const VideoFramePtr& image, std::vector<AiDetectRstEl>& io_rst,
                                            bool use_box) {
    if (!image->GetData() || io_rst.empty()) {
        return util::ErrorEnum::Success;
    }

    std::vector<VideoFramePtr> images;
    images.push_back(image);
    for (auto& io_el : io_rst) {
        std::vector<AiDetectRstEl> io_puts;
        io_puts.push_back(io_el);
        auto ret = Classify(images, io_puts, use_box);
        if (util::ErrorEnum::Success != ret) {
            LOG_INFO("{}", "Classify Fail");
        }
        io_el = io_puts[0];
    }

    return util::ErrorEnum::Success;
}

// Collect box or landmark coordinates from a detection element
std::vector<int> AiClassifierUnify::CollectBoxOrLandmarkData(const AiDetectRstEl& io_el, bool use_box) {
    std::vector<int> data;
    if (use_box) {
        const auto& box = io_el.relatedEl.bActive ? io_el.relatedEl.box : io_el.box;
        data.push_back(box.x);
        data.push_back(box.y);
        data.push_back(box.width);
        data.push_back(box.height);
    } else {
        const auto& lm = io_el.relatedEl.bActive ? io_el.relatedEl.landmark : io_el.landmark;
        for (const auto& point : lm.landmark) {
            data.push_back(point.x);
            data.push_back(point.y);
        }
    }
    return data;
}

// Append classification results to a single detection element
void AiClassifierUnify::AppendClassifyResults(AiDetectRstEl& io_el,
                                              const std::vector<cosmo::nn::ObjectInfoV1>& obj) {
    if (obj.empty()) {
        return;
    }
    for (const auto& info : obj.at(0).infos) {
        AiConfidence conf_el;
        conf_el.atomic_code = atomic_code_;
        conf_el.confidence  = info.confidence;
        conf_el.label       = info.class_name;
        io_el.classifyRst.push_back(conf_el);
        if (io_el.relatedEl.bActive) {
            io_el.relatedEl.classifyRst.push_back(conf_el);
        }
    }
}

/* Input data must not exceed hardware-supported max batch; most edge devices support single batch only */
util::ErrorEnum AiClassifierUnify::Classify(const std::vector<VideoFramePtr>& images,
                                            std::vector<AiDetectRstEl>& io_rst, bool use_box) {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (io_rst.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != io_rst.size()) {
        LOG_ERRO("{}", "Images Not Equal Rect Num");
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::vector<int>> datas;
    datas.reserve(io_rst.size());
    std::transform(io_rst.begin(), io_rst.end(), std::back_inserter(datas),
                   [this, use_box](const auto& io_el) { return CollectBoxOrLandmarkData(io_el, use_box); });

    std::vector<std::shared_ptr<cosmo::nn::Blob>> data_blobs{};
    ret = use_box ? ConvertDatasToBlobs(datas, data_blobs) : ConvertDatasToBlobsFloat(datas, data_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertDatasToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    auto status = classifier_->Forward({image_blobs, data_blobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
    status = classifier_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
    if (!bool(status)) {
        LOG_ERRO("ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    for (size_t i = 0; i < outputs.size(); i++) {
        AppendClassifyResults(io_rst[i], outputs.at(i));
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiClassifierUnify::ClassifyMultSub(const std::vector<VideoFramePtr>& images,
                                                   std::vector<std::vector<AiDetectRstEl>>& io_rst,
                                                   const bool& use_mult_sub, bool use_box) {
    if (!use_mult_sub) {
        return Classify(images, io_rst, use_box);
    }
    if (io_rst.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != io_rst.size()) {
        LOG_ERRO("{}", "Images Not Equal Rect Num");
        return util::ErrorEnum::InvalidParam;
    }
    size_t i = 0;
    for (i = 0; i < images.size(); i++) {
        std::vector<VideoFramePtr> imageInputs;
        imageInputs.push_back(images[i]);
        for (size_t j = 0; j < io_rst[i].size(); j++) {
            for (size_t k = 0; k < io_rst[i][j].relatedEls.size(); k++) {
                AiDetectRstEl el;
                el.box = io_rst[i][j].relatedEls[k].box;
                std::vector<AiDetectRstEl> io_puts;
                io_puts.push_back(el);

                auto ret = Classify(imageInputs, io_puts, use_box);
                if (util::ErrorEnum::Success != ret) {
                    LOG_INFO("{}", "Classify Fail");
                    return ret;
                }
                io_rst[i][j].classifyRst.insert(io_rst[i][j].classifyRst.begin(),
                                                io_puts[0].classifyRst.begin(), io_puts[0].classifyRst.end());
                io_rst[i][j].relatedEls[k].classifyRst.insert(io_rst[i][j].relatedEls[k].classifyRst.end(),
                                                              io_puts[0].classifyRst.begin(),
                                                              io_puts[0].classifyRst.end());
            }
        }
    }
    return util::ErrorEnum::Success;
}

void AiClassifierUnify::DispatchBatchResults(const std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs,
                                             const std::vector<std::pair<size_t, size_t>>& indexes,
                                             std::vector<std::vector<AiDetectRstEl>>& io_rst) {
    size_t out = 0;
    for (const auto& output : outputs) {
        for (const auto& outputEl : output) {
            if (out >= indexes.size()) {
                break;
            }
            size_t img_index = indexes[out].first;
            size_t box_index = indexes[out].second;
            for (const auto& info : outputEl.infos) {
                AiConfidence conf_el;
                conf_el.atomic_code = atomic_code_;
                conf_el.confidence  = info.confidence;
                conf_el.label       = info.class_name;
                io_rst[img_index][box_index].classifyRst.push_back(conf_el);
                if (io_rst[img_index][box_index].relatedEl.bActive) {
                    io_rst[img_index][box_index].relatedEl.classifyRst.push_back(conf_el);
                }
            }
            out++;
        }
    }
}

util::ErrorEnum AiClassifierUnify::Classify(const std::vector<VideoFramePtr>& images,
                                            std::vector<std::vector<AiDetectRstEl>>& io_rst, bool use_box) {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (images.empty() || io_rst.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != io_rst.size()) {
        LOG_ERRO("{}", "Input Datas Not Match");
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<VideoFramePtr> input_images;
    std::vector<std::vector<std::vector<int>>> input_datas;
    std::vector<std::pair<size_t, size_t>> indexes;
    size_t total = std::accumulate(io_rst.begin(), io_rst.end(), size_t{0},
                                   [](size_t sum, const auto& v) { return sum + v.size(); });
    size_t count = 0;
    for (size_t i = 0; i < io_rst.size(); i++) {
        std::vector<std::vector<int>> datas;
        for (size_t j = 0; j < io_rst[i].size(); j++) {
            datas.push_back(CollectBoxOrLandmarkData(io_rst[i][j], use_box));
            indexes.push_back(std::make_pair(i, j));
            count++;
            if ((count % max_batch_size_ == 0) || (count == total)) {
                input_datas.push_back(datas);
                size_t img = static_cast<size_t>(-1);
                for (size_t k = 0; k < indexes.size(); k++) {
                    if (img != indexes[k].first) {
                        img = indexes[k].first;
                        input_images.push_back(images[img]);
                    }
                }
                std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
                auto ret = Forward(input_images, input_datas, outputs, use_box);
                if (util::ErrorEnum::Success != ret) {
                    LOG_ERRO("Forward Failed. Ret:{}", ret);
                }
                DispatchBatchResults(outputs, indexes, io_rst);
                input_images.clear();
                input_datas.clear();
                indexes.clear();
                datas.clear();
            }
        }
        if (!datas.empty()) {
            input_datas.push_back(datas);
        }
    }

    return util::ErrorEnum::Success;
}

// Forward inference — moved to AiClassifierForward.cc

}  // namespace cosmo
