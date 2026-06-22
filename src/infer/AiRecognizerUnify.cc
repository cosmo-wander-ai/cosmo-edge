// AiRecognizerUnify — Ai Recognizer Unify implementation.

#include "infer/AiRecognizerUnify.h"

#include "util/Log.h"

namespace cosmo {
AiRecognizerUnify::AiRecognizerUnify(const std::string& json_path, const std::string& model_path)
    : cfg_path_(json_path), model_path_(model_path) {}

AiRecognizerUnify::~AiRecognizerUnify() {
    LOG_INFO("{}", "AiRecognizerUnify Delete");
}

util::ErrorEnum AiRecognizerUnify::Init() {
    if (recognizer_) {
        LOG_WARN("{}", "Init SDK Recognizer Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    try {
        recognizer_ = std::make_unique<cosmo::nn::DefaultComponent>(cfg_path_, model_path_, GetDeviceType(),
                                                                    &profiler_);
    } catch (const std::exception& e) {
        LOG_ERRO("Init SDK Recognizer Failed. CfgPath:{} ModelPath:{}, {}", cfg_path_, model_path_, e.what());
        recognizer_.reset();
        return util::ErrorEnum::Failed;
    }

    if (!recognizer_) {
        LOG_WARN("Init SDK Recognizer Failed. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);
        return util::ErrorEnum::Failed;
    }
    LOG_DEBUG("Recognizer {} Init", model_path_);

    max_batch_size_ = static_cast<size_t>(recognizer_->GetMaxBatchSize());
    LOG_DEBUG("Recognizer max_batch_size_ {}", max_batch_size_);

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiRecognizerUnify::Extract(VideoFramePtr image, std::vector<AiDetectRstEl>& io_rst,
                                           bool use_box) {
    if (!image->GetData() || io_rst.empty()) {
        return util::ErrorEnum::Success;
    }

    std::vector<VideoFramePtr> images;
    images.push_back(image);
    for (auto& io_el : io_rst) {
        std::vector<AiDetectRstEl> io_puts;
        io_puts.push_back(io_el);
        auto ret = Extract(images, io_puts, use_box);
        if (util::ErrorEnum::Success != ret) {
            LOG_INFO("{}", "Classify Fail");
        }
        io_el = io_puts[0];
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiRecognizerUnify::Extract(const std::vector<VideoFramePtr>& images,
                                           std::vector<AiDetectRstEl>& io_rst, bool use_box) {
    if (!recognizer_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (io_rst.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != io_rst.size()) {
        LOG_ERRO("Input Datas Not Match, Images:{} io_rst:{}", images.size(), io_rst.size());
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::vector<int>> datas;
    for (auto& io_el : io_rst) {
        std::vector<int> data;
        if (use_box) {
            if (io_el.relatedEl.bActive) {
                data.push_back(io_el.relatedEl.box.x);
                data.push_back(io_el.relatedEl.box.y);
                data.push_back(io_el.relatedEl.box.width);
                data.push_back(io_el.relatedEl.box.height);
            } else {
                data.push_back(io_el.box.x);
                data.push_back(io_el.box.y);
                data.push_back(io_el.box.width);
                data.push_back(io_el.box.height);
            }
        } else {
            if (io_el.relatedEl.bActive) {
                for (auto point : io_el.relatedEl.landmark.landmark) {
                    data.push_back(point.x);
                    data.push_back(point.y);
                }
            } else {
                for (auto point : io_el.landmark.landmark) {
                    data.push_back(point.x);
                    data.push_back(point.y);
                }
            }
        }
        datas.push_back(data);
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> data_blobs{};
    if (use_box) {
        ret = ConvertDatasToBlobs(datas, data_blobs);
    } else {
        ret = ConvertDatasToBlobsFloat(datas, data_blobs);
    }
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertDatasToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    auto status = recognizer_->Forward({image_blobs, data_blobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    std::vector<std::vector<float>> outputs{};
    status = recognizer_->ParseOutput<float>(outputs);
    if (!bool(status)) {
        LOG_ERRO("ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    for (size_t i = 0; i < outputs.size(); i++) {
        if ((io_rst[i].relatedEl.bActive)) {
            io_rst[i].feature.feature           = outputs[i];
            io_rst[i].relatedEl.feature.feature = outputs[i];
        } else {
            io_rst[i].feature.feature = outputs[i];
        }
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiRecognizerUnify::Extract(const std::vector<VideoFramePtr>& images,
                                           std::vector<std::vector<AiDetectRstEl>>& io_rst, bool use_box) {
    if (!recognizer_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (images.empty() || io_rst.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != io_rst.size()) {
        LOG_ERRO("Input Datas Not Match, Images:{} io_rst:{}", images.size(), io_rst.size());
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<VideoFramePtr> input_images;
    std::vector<std::vector<std::vector<int>>> input_datas;
    std::vector<std::pair<size_t, size_t>> indexes;
    size_t total = 0;
    for (size_t i = 0; i < io_rst.size(); i++) {
        total += io_rst[i].size();
    }
    size_t count = 0;
    for (size_t i = 0; i < io_rst.size(); i++) {
        std::vector<std::vector<int>> datas;
        for (size_t j = 0; j < io_rst[i].size(); j++) {
            std::vector<int> data;
            auto& io_el = io_rst[i][j];
            if (use_box) {
                if (io_el.relatedEl.bActive) {
                    data.push_back(io_el.relatedEl.box.x);
                    data.push_back(io_el.relatedEl.box.y);
                    data.push_back(io_el.relatedEl.box.width);
                    data.push_back(io_el.relatedEl.box.height);
                } else {
                    data.push_back(io_el.box.x);
                    data.push_back(io_el.box.y);
                    data.push_back(io_el.box.width);
                    data.push_back(io_el.box.height);
                }
            } else {
                if (io_el.relatedEl.bActive) {
                    for (auto point : io_el.relatedEl.landmark.landmark) {
                        data.push_back(point.x);
                        data.push_back(point.y);
                    }
                } else {
                    for (auto point : io_el.landmark.landmark) {
                        data.push_back(point.x);
                        data.push_back(point.y);
                    }
                }
            }

            datas.push_back(data);
            indexes.push_back(std::make_pair(i, j));
            count++;
            if ((count % max_batch_size_ == 0) || (count == total)) {
                input_datas.push_back(datas);
                size_t img = static_cast<size_t>(-1);
                for (size_t k = 0; k < indexes.size(); k++) {
                    auto index = indexes[k];
                    if (img != index.first) {
                        img = index.first;
                        input_images.push_back(images[img]);
                    }
                }
                std::vector<std::vector<float>> outputs;
                auto ret = Forward(input_images, input_datas, outputs, use_box);
                if (util::ErrorEnum::Success != ret) {
                    LOG_ERRO("Forward Failed. Ret:{}", ret);
                }
                size_t out = 0;
                for (auto output : outputs) {
                    size_t img_index = indexes[out].first;
                    size_t box_index = indexes[out].second;
                    if ((io_rst[img_index][box_index].relatedEl.bActive)) {
                        io_rst[img_index][box_index].relatedEl.feature.feature = output;
                    } else {
                        io_rst[img_index][box_index].feature.feature = output;
                    }
                    out++;
                }
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

util::ErrorEnum AiRecognizerUnify::Forward(const std::vector<VideoFramePtr>& images,
                                           const std::vector<std::vector<std::vector<int>>>& datas,
                                           std::vector<std::vector<float>>& features, bool use_box) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> data_blobs{};
    if (use_box) {
        ret = ConvertDatasToBlobs(datas, data_blobs);
    } else {
        ret = ConvertDatasToBlobsFloat(datas, data_blobs);
    }

    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertDatasToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    auto status = recognizer_->Forward({image_blobs, data_blobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    status = recognizer_->ParseOutput<float>(features);
    if (!bool(status)) {
        LOG_ERRO("ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }
    return util::ErrorEnum::Success;
}

std::vector<float> AiRecognizerUnify::GetScoreLevel() {
    if (!recognizer_) {
        LOG_WARN("{}", "SDK Recognizer Not Init");
        return std::vector<float>();
    }
    return recognizer_->GetScoreLevels();
}

float AiRecognizerUnify::CompareFeature(const AiFeature& feature1, const AiFeature& feature2) {
    if (feature1.feature.empty() || feature2.feature.empty()) {
        return 0.f;
    }
    if (feature1.feature.size() != feature2.feature.size()) {
        LOG_WARN("Feature1 And Feature2 Not Equal {}!={}", feature1.feature.size(), feature2.feature.size());
        return 0.f;
    }
    std::vector<float> scores = GetScoreLevel();
    if (scores.size() < 3) {
        LOG_WARN("{}", "Score Level Invalid");
        return 0.f;
    }

    float distance        = 0.f;
    size_t feature_length = feature1.feature.size();
    for (size_t i = 0; i < feature_length; i++) {
        distance += (feature1.feature[i] - feature2.feature[i]) * (feature1.feature[i] - feature2.feature[i]);
    }
    float score = 0;
    if (distance < 0.333f * scores[0]) {
        score = 100.0f;
    }
    if (distance >= 0.333f * scores[0] && distance < scores[0]) {
        score = ((1.5f * (scores[0] - distance) / scores[0] * 0.2f + 0.8f) * 100.f);
    }
    if ((distance >= scores[0]) && (distance <= scores[1])) {
        score = ((0.8f - (distance - scores[0]) / ((scores[1] - scores[0])) * 0.2f) * 100.f);
    }
    if ((distance > scores[1]) && (distance <= scores[2])) {
        score = (((scores[2] - distance) / (scores[2] - scores[1]) * 0.6f) * 100.f);
    }
    if (distance > scores[2]) {
        score = 0;
    }

    return score;
}

util::ErrorEnum AiRecognizerUnify::GetMaxBatchSize(size_t& value) {
    if (!recognizer_) {
        LOG_WARN("{}", "SDK Recognizer Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(recognizer_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}
}  // namespace cosmo