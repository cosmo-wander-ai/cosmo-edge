// AiLandmarkerUnify — Ai Landmarker Unify implementation.

#include "infer/AiLandmarkerUnify.h"

#include <algorithm>

#include "util/Log.h"

namespace cosmo {
AiLandmarkerUnify::AiLandmarkerUnify(const std::string& jsonPath, const std::string& modelPath)
    : cfg_path_(jsonPath), model_path_(modelPath) {}

AiLandmarkerUnify::~AiLandmarkerUnify() {
    LOG_INFO("{}", "AiLandmarkerUnify Delete");
}

util::ErrorEnum AiLandmarkerUnify::Init() {
    if (landmarker_) {
        LOG_WARN("{}", "Init SDK Landmarker Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    landmarker_ =
        std::make_unique<cosmo::nn::DefaultComponent>(cfg_path_, model_path_, GetDeviceType(), &profiler_);
    LOG_DEBUG("Landmarker {} Init", model_path_);

    max_batch_size_ = static_cast<size_t>(landmarker_->GetMaxBatchSize());
    LOG_DEBUG("{} Landmarker Max Batch Size {}", model_path_, max_batch_size_);

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiLandmarkerUnify::Marker(VideoFramePtr image, std::vector<AiDetectRstEl>& ioRst) {
    if (!image->GetData() || ioRst.empty()) {
        return util::ErrorEnum::Success;
    }

    std::vector<VideoFramePtr> images;
    images.push_back(image);
    for (auto& ioEl : ioRst) {
        std::vector<AiDetectRstEl> ioPuts{};
        ioPuts.push_back(ioEl);
        auto ret = Marker(images, ioPuts);
        if (util::ErrorEnum::Success != ret) {
            LOG_INFO("{}", "Marker Fail");
        }
        ioEl = ioPuts[0];
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiLandmarkerUnify::Marker(const std::vector<VideoFramePtr>& images,
                                          std::vector<AiDetectRstEl>& ioRst) {
    if (!landmarker_) {
        LOG_WARN("{}", "SDK Marker Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (ioRst.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != ioRst.size()) {
        LOG_ERRO("{}", "Images Not Equal Rect Num");
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> imageBlobs{};
    auto ret = ConvertImagesToBlobs(images, imageBlobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::vector<int>> rects;
    std::transform(ioRst.begin(), ioRst.end(), std::back_inserter(rects),
                   [this](const auto& ioEl) { return CollectRectData(ioEl); });

    std::vector<std::shared_ptr<cosmo::nn::Blob>> rectBlobs{};
    ret = ConvertDatasToBlobs(rects, rectBlobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertDatasToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    auto status = landmarker_->Forward({imageBlobs, rectBlobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }
    std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
    status = landmarker_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
    if (!bool(status)) {
        LOG_ERRO("ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    for (size_t i = 0; i < outputs.size(); i++) {
        const auto& obj = outputs.at(i);
        if (obj.empty()) {
            continue;
        }
        auto& lm =
            ioRst[i].relatedEl.bActive ? ioRst[i].relatedEl.landmark.landmark : ioRst[i].landmark.landmark;
        std::transform(obj.at(0).key_points.begin(), obj.at(0).key_points.end(), std::back_inserter(lm),
                       [](const auto& point) {
                           return util::Point(static_cast<int>(point.first), static_cast<int>(point.second));
                       });
    }
    return util::ErrorEnum::Success;
}

// Collect bounding box coordinates from a detection element
std::vector<int> AiLandmarkerUnify::CollectRectData(const AiDetectRstEl& ioEl) {
    std::vector<int> rect;
    const auto& box = ioEl.relatedEl.bActive ? ioEl.relatedEl.box : ioEl.box;
    rect.push_back(box.x);
    rect.push_back(box.y);
    rect.push_back(box.width);
    rect.push_back(box.height);
    return rect;
}

// Dispatch batch landmark results to the corresponding detection elements
void AiLandmarkerUnify::DispatchLandmarkResults(
    const std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs,
    const std::vector<std::pair<int, int>>& indexes, std::vector<std::vector<AiDetectRstEl>>& ioRst) {
    int out = 0;
    for (const auto& output : outputs) {
        for (const auto& outputEl : output) {
            if (out >= static_cast<int>(indexes.size())) {
                break;
            }
            int imgIndex = indexes[static_cast<size_t>(out)].first;
            int boxIndex = indexes[static_cast<size_t>(out)].second;
            auto& target = ioRst[static_cast<size_t>(imgIndex)][static_cast<size_t>(boxIndex)];
            auto& lm =
                target.relatedEl.bActive ? target.relatedEl.landmark.landmark : target.landmark.landmark;
            std::transform(outputEl.key_points.begin(), outputEl.key_points.end(), std::back_inserter(lm),
                           [](const auto& point) {
                               return util::Point(static_cast<int>(point.first),
                                                  static_cast<int>(point.second));
                           });
            out++;
        }
    }
}

util::ErrorEnum AiLandmarkerUnify::Marker(const std::vector<VideoFramePtr>& images,
                                          std::vector<std::vector<AiDetectRstEl>>& ioRst) {
    if (!landmarker_) {
        LOG_WARN("{}", "SDK Marker Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (images.empty() || ioRst.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != ioRst.size()) {
        LOG_ERRO("{}", "Input Datas Not Match");
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<VideoFramePtr> inputImages;
    std::vector<std::vector<std::vector<int>>> inputRects;
    std::vector<std::pair<int, int>> indexes;
    size_t total = 0;
    for (const auto& rst : ioRst) {
        total += rst.size();
    }
    size_t count = 0;
    for (size_t i = 0; i < ioRst.size(); i++) {
        std::vector<std::vector<int>> rects;
        for (size_t j = 0; j < ioRst[i].size(); j++) {
            rects.push_back(CollectRectData(ioRst[i][j]));
            indexes.push_back(std::make_pair(i, j));
            count++;
            if ((count % max_batch_size_ == 0) || (count == total)) {
                inputRects.push_back(rects);
                int img = -1;
                for (size_t k = 0; k < indexes.size(); k++) {
                    if (img != indexes[k].first) {
                        img = indexes[k].first;
                        inputImages.push_back(images[static_cast<size_t>(img)]);
                    }
                }
                std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
                auto ret = Forward(inputImages, inputRects, outputs);
                if (util::ErrorEnum::Success != ret) {
                    LOG_ERRO("Forward Failed. Ret:{}", ret);
                }
                DispatchLandmarkResults(outputs, indexes, ioRst);
                inputImages.clear();
                inputRects.clear();
                indexes.clear();
                rects.clear();
            }
        }
        if (!rects.empty()) {
            inputRects.push_back(rects);
        }
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiLandmarkerUnify::Forward(const std::vector<VideoFramePtr>& images,
                                           const std::vector<std::vector<std::vector<int>>>& rects,
                                           std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> imageBlobs{};
    auto ret = ConvertImagesToBlobs(images, imageBlobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> rectBlobs{};
    ret = ConvertDatasToBlobs(rects, rectBlobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertDatasToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    auto status = landmarker_->Forward({imageBlobs, rectBlobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    status = landmarker_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
    if (!bool(status)) {
        LOG_ERRO("ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiLandmarkerUnify::GetMaxBatchSize(size_t& value) {
    if (!landmarker_) {
        LOG_WARN("{}", "SDK Marker Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(landmarker_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}
}  // namespace cosmo