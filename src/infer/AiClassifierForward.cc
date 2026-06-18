// AiClassifierForward.cc — Forward inference for AiClassifierUnify.
// Split from AiClassifierUnify.cc to reduce file size (DEBT-007).

#include "infer/AiClassifierUnify.h"
#include "util/Log.h"

namespace cosmo {

util::ErrorEnum AiClassifierUnify::Classify(const VideoFramePtr& image_base, const VideoFramePtr& image,
                                            AiDetectRstEl& io_rst) {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    if (!image_base->GetData() || !image->GetData()) {
        return util::ErrorEnum::Success;
    }

    auto image_base_blob = ConvertImageToBlob(image_base);
    auto image_blob      = ConvertImageToBlob(image);

    auto status = classifier_->Forward({{image_base_blob}, {image_blob}});
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

    if (outputs.size() > 0) {
        std::vector<cosmo::nn::ObjectInfoV1> obj = outputs.at(0);
        if (!obj.empty()) {
            io_rst.box =
                util::Box(0, 0, static_cast<int>(image->GetWidth()), static_cast<int>(image->GetHeight()));
            io_rst.confidence.confidence = 1.0;
            auto classify_infos          = obj.at(0).infos;
            for (auto info : classify_infos) {
                AiConfidence conf_el;
                conf_el.atomic_code = atomic_code_;
                conf_el.confidence  = info.confidence;
                conf_el.label       = info.class_name;
                io_rst.classifyRst.push_back(conf_el);
            }
        }
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiClassifierUnify::Classify(const std::vector<VideoFramePtr>& images_base,
                                            const std::vector<VideoFramePtr>& images,
                                            std::vector<AiDetectRstEl>& io_rst) {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }

    if (images_base.empty()) {
        return util::ErrorEnum::Success;
    }

    if (images_base.size() != images.size()) {
        LOG_ERRO("{}", "Images Not Equal Base");
        return util::ErrorEnum::InvalidParam;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> images_base_blobs{};
    auto ret = ConvertImagesToBlobs(images_base, images_base_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs (images_base) Failed. Ret:{}", ret);
        return ret;
    }

    std::vector<std::shared_ptr<cosmo::nn::Blob>> images_blobs{};
    ret = ConvertImagesToBlobs(images, images_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs (images) Failed. Ret:{}", ret);
        return ret;
    }

    auto status = classifier_->Forward({images_base_blobs, images_blobs});
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

    if (outputs.size() != images.size()) {
        LOG_ERRO("{}", "Outputs Size Not Equal Images Size.");
        return util::ErrorEnum::AI_CLASSIFY_FAILED;
    }

    for (size_t i = 0; i < outputs.size(); i++) {
        AiDetectRstEl rst_el;
        rst_el.box                               = util::Box(0, 0, static_cast<int>(images[i]->GetWidth()),
                                                             static_cast<int>(images[i]->GetHeight()));
        rst_el.confidence.confidence             = 1.0;
        std::vector<cosmo::nn::ObjectInfoV1> obj = outputs.at(i);
        if (obj.empty()) {
            io_rst.push_back(rst_el);
            continue;
        }
        auto classify_infos = obj.at(0).infos;
        for (auto info : classify_infos) {
            AiConfidence conf_el;
            conf_el.atomic_code = atomic_code_;
            conf_el.confidence  = info.confidence;
            conf_el.label       = info.class_name;
            rst_el.classifyRst.push_back(conf_el);
        }
        io_rst.push_back(rst_el);
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiClassifierUnify::Forward(const std::vector<VideoFramePtr>& images,
                                           const std::vector<std::vector<std::vector<int>>>& datas,
                                           std::vector<std::vector<cosmo::nn::ObjectInfoV1>>& outputs,
                                           bool use_box) {
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

    auto status = classifier_->Forward({image_blobs, data_blobs});
    if (!bool(status)) {
        LOG_ERRO("Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    status = classifier_->ParseOutput<cosmo::nn::ObjectInfoV1>(outputs);
    if (!bool(status)) {
        LOG_ERRO("ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum AiClassifierUnify::GetMaxBatchSize(size_t& value) const {
    if (!classifier_) {
        LOG_WARN("{}", "SDK Classifier Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(classifier_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiClassifierUnify::GetAttributeCategory(AiDetectRstEl& io_rst) {
    for (auto classifyRst : io_rst.classifyRst) {
        cosmo::nn::CategoryInfo info;
        auto status = classifier_->GetCategoryInfo(classifyRst.label, info);
        if (!bool(status)) {
            LOG_ERRO("GetCategoryInfo Failed.({} : {}/{})", status.description(), classifyRst.atomic_code,
                     classifyRst.label);
            return util::ErrorEnum::Failed;
        }

        AiAttribute attr;
        attr.category    = info.class_name;
        attr.label       = classifyRst.label;
        attr.atomic_code = classifyRst.atomic_code;
        attr.confidence  = classifyRst.confidence;
        io_rst.attrRst.push_back(attr);
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum AiClassifierUnify::GetAttributeCategory(std::vector<AiDetectRstEl>& io_rst) {
    for (auto& rst : io_rst) {
        auto ret = GetAttributeCategory(rst);
        if (ret != util::ErrorEnum::Success) {
            return ret;
        }
    }
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
