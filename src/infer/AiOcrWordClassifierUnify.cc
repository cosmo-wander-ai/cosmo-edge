// AiOcrWordClassifierUnify — OCR word classifier model wrapper.

#include "infer/AiOcrWordClassifierUnify.h"

#include "util/Log.h"

namespace cosmo {

AiOcrWordClassifierUnify::AiOcrWordClassifierUnify(const std::string& alg_code, const std::string& json_path,
                                                   const std::string& model_path,
                                                   const std::string& word_dict_path)
    : json_path_(json_path),
      model_path_(model_path),
      word_dict_path_(word_dict_path),
      atomic_code_(alg_code) {}

AiOcrWordClassifierUnify::~AiOcrWordClassifierUnify() {
    LOG_INFO("{}", "AiOcrWordClassifierUnify Delete");
}

util::ErrorEnum AiOcrWordClassifierUnify::Init() {
    if (classifier_) {
        LOG_WARN("{}", "Init SDK Classifier Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    if (json_path_.empty() || model_path_.empty()) {
        LOG_ERRO("Model paths are empty. AlgCode:{}", atomic_code_);
        return util::ErrorEnum::Failed;
    }
    classifier_ = std::make_unique<cosmo::nn::DefaultComponent>(json_path_, model_path_, GetDeviceType(),
                                                                &profiler_, "", word_dict_path_);
    LOG_DEBUG("Classifier {} Init", atomic_code_);

    max_batch_size_ = static_cast<size_t>(classifier_->GetMaxBatchSize());
    LOG_DEBUG("{} Classifier Max Batch Size {}", atomic_code_, max_batch_size_);

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiOcrWordClassifierUnify::Classify(const VideoFramePtr& image, const util::Box& box,
                                                   std::string& rst) {
    if (!image->GetData()) {
        LOG_ERRO("{}", "Image Data Illegal.");
        return util::ErrorEnum::InvalidParam;
    }
    if (box.x < 0 || box.y < 0 || box.width <= 0 || box.height <= 0) {
        LOG_ERRO("Box Illegal. x:{} y:{} width:{} height:{}", box.x, box.y, box.width, box.height);
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<std::string> results;
    std::vector<VideoFramePtr> images = {image};
    std::vector<util::Box> boxes      = {box};
    auto ret                          = Classify(images, boxes, results);
    if (util::ErrorEnum::Success != ret) {
        LOG_INFO("Classify Failed. Ret:{}", ret);
        return ret;
    }
    if (!results.empty()) {
        rst = results[0];
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiOcrWordClassifierUnify::Classify(const std::vector<VideoFramePtr>& images,
                                                   const std::vector<util::Box>& boxes,
                                                   std::vector<std::string>& rsts) {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }

    if (images.size() != boxes.size()) {
        LOG_ERRO("{}", "Images Num Not Equal Box Num");
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::vector<int>> datas;
    for (const auto& box : boxes) {
        std::vector<int> data;
        data.push_back(box.x);
        data.push_back(box.y);
        data.push_back(box.width);
        data.push_back(box.height);
        datas.push_back(data);
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> data_blobs{};
    ret = ConvertDatasToBlobs(datas, data_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertDatasToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    auto status = classifier_->Forward({image_blobs, data_blobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    std::vector<std::vector<char>> plates;
    status = classifier_->ParseOutput<char>(plates);
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    for (const auto& plate : plates) {
        std::string rst(plate.begin(), plate.end());
        rsts.push_back(rst);
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiOcrWordClassifierUnify::Classify(const std::vector<VideoFramePtr>& images,
                                                   const std::vector<std::vector<util::Box>>& boxes,
                                                   std::vector<std::vector<std::string>>& rsts) {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (images.empty() || boxes.empty()) {
        return util::ErrorEnum::Success;
    }
    if (images.size() != boxes.size()) {
        LOG_ERRO("{}", "Input Datas Not Match");
        return util::ErrorEnum::InvalidParam;
    }

    rsts.resize(images.size());

    std::vector<VideoFramePtr> input_images;
    std::vector<std::vector<std::vector<int>>> input_rects;
    std::vector<std::pair<int, int>> indexes;
    size_t total = 0;
    for (const auto& rects : boxes) {
        total += rects.size();
    }
    size_t count = 0;
    for (size_t i = 0; i < boxes.size(); i++) {
        std::vector<std::vector<int>> rects;
        for (size_t j = 0; j < boxes[i].size(); j++) {
            std::vector<int> rect;
            auto box = boxes[i][j];
            rect.push_back(box.x);
            rect.push_back(box.y);
            rect.push_back(box.width);
            rect.push_back(box.height);

            rects.push_back(rect);
            indexes.push_back(std::make_pair(i, j));
            count++;
            if ((count % max_batch_size_ == 0) || (count == total)) {
                input_rects.push_back(rects);
                int img_index = -1;
                for (size_t k = 0; k < indexes.size(); k++) {
                    auto index = indexes[k];
                    if (img_index != index.first) {
                        img_index = index.first;
                        input_images.push_back(images[static_cast<size_t>(img_index)]);
                    }
                }
                std::vector<std::vector<char>> outputs;
                auto ret = Forward(input_images, input_rects, outputs);
                if (util::ErrorEnum::Success != ret) {
                    LOG_ERRO("Forward Failed. Ret:{}", ret);
                }
                int out = 0;
                for (const auto& output : outputs) {
                    std::string plate(output.begin(), output.end());
                    int current_img_index = indexes[static_cast<size_t>(out)].first;
                    rsts[static_cast<size_t>(current_img_index)].push_back(plate);
                    out++;
                }
                input_images.clear();
                input_rects.clear();
                indexes.clear();
                rects.clear();
            }
        }
        if (!rects.empty()) {
            input_rects.push_back(rects);
        }
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiOcrWordClassifierUnify::GetMaxBatchSize(size_t& value) const {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(classifier_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiOcrWordClassifierUnify::Forward(const std::vector<VideoFramePtr>& images,
                                                  const std::vector<std::vector<std::vector<int>>>& datas,
                                                  std::vector<std::vector<char>>& outputs) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> data_blobs{};
    ret = ConvertDatasToBlobs(datas, data_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertDatasToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    auto status = classifier_->Forward({image_blobs, data_blobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    status = classifier_->ParseOutput<char>(outputs);
    if (!bool(status)) {
        LOG_ERRO("ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiOcrWordClassifierUnify::WarpAffine(std::shared_ptr<cosmo::nn::Blob> blob,
                                                     std::vector<std::pair<float, float>>& src_points,
                                                     std::shared_ptr<cosmo::nn::Blob>& out_blob) {
    auto status = cosmo::nn::NetUtils::WarpAffine(blob, src_points, out_blob);
    if (!bool(status)) {
        LOG_ERRO("WarpAffine Failed.({})", status.description());
        return util::ErrorEnum::Failed;
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiOcrWordClassifierUnify::WarpAffine(const VideoFramePtr& in_image,
                                                     const AiLandmarkData& landmark,
                                                     const VideoFramePtr& out_image) {
    if (!in_image || !out_image) {
        return util::ErrorEnum::NoMem;
    }
    if (landmark.landmark.empty()) {
        return util::ErrorEnum::InvalidParam;
    }
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    auto blob     = ConvertImageToBlob(in_image);
    auto out_blob = ConvertImageToBlob(out_image);
    if (!blob || !out_blob) {
        LOG_WARN("{}", "SDK ConvertImage Failed");
        return util::ErrorEnum::NoMem;
    }
    std::vector<std::pair<float, float>> src_points;
    for (const auto& landmark_unit : landmark.landmark) {
        auto p =
            std::pair<float, float>(static_cast<float>(landmark_unit.x), static_cast<float>(landmark_unit.y));
        src_points.push_back(p);
    }
    return WarpAffine(blob, src_points, out_blob);
}

}  // namespace cosmo