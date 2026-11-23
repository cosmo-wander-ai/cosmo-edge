// AiLandmarkerUnify — Ai Landmarker Unify implementation.

#include "infer/AiLandmarkerUnify.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "util/Log.h"

namespace cosmo {
namespace {
    bool ValidLandmarkOutputs(const std::vector<std::vector<nn::ObjectInfoV1>>& outputs, size_t expected) {
        if (outputs.size() != expected)
            return false;
        for (const auto& group : outputs) {
            if (group.size() != 1 || group[0].key_points.empty())
                return false;
            for (const auto& point : group[0].key_points) {
                if (!std::isfinite(point.first) || !std::isfinite(point.second) ||
                    static_cast<double>(point.first) < std::numeric_limits<int>::min() ||
                    static_cast<double>(point.first) > std::numeric_limits<int>::max() ||
                    static_cast<double>(point.second) < std::numeric_limits<int>::min() ||
                    static_cast<double>(point.second) > std::numeric_limits<int>::max())
                    return false;
            }
        }
        return true;
    }
}  // namespace

AiLandmarkerUnify::AiLandmarkerUnify(const std::string& jsonPath, const std::string& modelPath)
    : cfg_path_(jsonPath), model_path_(modelPath) {}

AiLandmarkerUnify::AiLandmarkerUnify(const std::string&, const std::string& jsonPath,
                                     const std::string& modelPath)
    : cfg_path_(jsonPath), model_path_(modelPath) {}

AiLandmarkerUnify::~AiLandmarkerUnify() {
    LOG_INFO("{}", "AiLandmarkerUnify Delete");
}

util::ErrorEnum AiLandmarkerUnify::Init() {
    if (landmarker_) {
        LOG_WARN("{}", "Init SDK Landmarker Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    cosmo::nn::DefaultComponent::Options options;
    landmarker_ =
        std::make_unique<cosmo::nn::DefaultComponent>(options, cfg_path_, model_path_, GetDeviceType());
    LOG_DEBUG("Landmarker {} Init", model_path_);

    const auto max_batch = landmarker_->GetMaxBatchSize();
    if (max_batch <= 0) {
        LOG_ERRO("Invalid landmarker maximum batch size");
        landmarker_.reset();
        return util::ErrorEnum::Failed;
    }
    max_batch_size_ = static_cast<size_t>(max_batch);
    LOG_DEBUG("{} Landmarker Max Batch Size {}", model_path_, max_batch_size_);

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiLandmarkerUnify::Marker(VideoFramePtr image, std::vector<AiDetectRstEl>& ioRst) {
    if (!VideoFrameValid(image) || !image->GetData())
        return util::ErrorEnum::FrameDataInvalid;
    if (ioRst.empty()) {
        return util::ErrorEnum::Success;
    }

    std::vector<VideoFramePtr> images;
    images.push_back(image);
    auto staged = ioRst;
    for (auto& ioEl : staged) {
        std::vector<AiDetectRstEl> ioPuts{};
        ioPuts.push_back(ioEl);
        auto ret = Marker(images, ioPuts);
        if (util::ErrorEnum::Success != ret) {
            LOG_INFO("{}", "Marker Fail");
            return ret;
        }
        ioEl = ioPuts[0];
    }
    ioRst = std::move(staged);
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
    if (!ValidLandmarkOutputs(outputs, ioRst.size())) {
        LOG_ERRO("Invalid landmark output count or coordinates");
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
    const std::vector<std::pair<size_t, size_t>>& indexes, std::vector<std::vector<AiDetectRstEl>>& ioRst) {
    size_t out = 0;
    for (const auto& output : outputs) {
        for (const auto& outputEl : output) {
            if (out >= indexes.size()) {
                break;
            }
            const size_t imgIndex = indexes[out].first;
            const size_t boxIndex = indexes[out].second;
            auto& target          = ioRst[imgIndex][boxIndex];
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
    if (!max_batch_size_) {
        LOG_ERRO("Invalid landmarker maximum batch size");
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<VideoFramePtr> inputImages;
    std::vector<std::vector<std::vector<int>>> inputRects;
    std::vector<std::pair<size_t, size_t>> indexes;
    auto staged  = ioRst;
    size_t total = 0;
    for (const auto& rst : ioRst) {
        if (rst.size() > std::numeric_limits<size_t>::max() - total)
            return util::ErrorEnum::InvalidParam;
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
                size_t img = static_cast<size_t>(-1);
                for (size_t k = 0; k < indexes.size(); k++) {
                    if (img != indexes[k].first) {
                        img = indexes[k].first;
                        inputImages.push_back(images[img]);
                    }
                }
                std::vector<std::vector<cosmo::nn::ObjectInfoV1>> outputs;
                auto ret = Forward(inputImages, inputRects, outputs);
                if (util::ErrorEnum::Success != ret) {
                    LOG_ERRO("Forward Failed. Ret:{}", ret);
                    return ret;
                }
                DispatchLandmarkResults(outputs, indexes, staged);
                inputImages.clear();
                inputRects.clear();
                indexes.clear();
                rects.clear();
            }
        }
        if (!rects.empty()) {
            inputRects.push_back(rects);
            rects.clear();
        }
    }
    ioRst = std::move(staged);
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiLandmarkerUnify::Forward(const std::vector<VideoFramePtr>& images,
                                           const std::vector<std::vector<std::vector<int>>>& rects,
                                           std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs) {
    outputs.clear();
    if (images.empty() || images.size() != rects.size() || !max_batch_size_)
        return util::ErrorEnum::InvalidParam;
    size_t expected = 0;
    for (const auto& group : rects) {
        if (group.empty() || group.size() > max_batch_size_ - expected)
            return util::ErrorEnum::InvalidParam;
        expected += group.size();
    }
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
    if (!ValidLandmarkOutputs(outputs, expected)) {
        outputs.clear();
        LOG_ERRO("Invalid landmark output count or coordinates");
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
