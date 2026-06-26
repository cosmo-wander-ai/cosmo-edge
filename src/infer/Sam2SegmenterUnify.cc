// Sam2SegmenterUnify.cc — SAM2 segmentation model implementation.

#include "infer/Sam2SegmenterUnify.h"

#include <algorithm>
#include <iterator>

#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo {
Sam2SegmenterUnify::Sam2SegmenterUnify(const std::string &atomicCode, const std::string &jsonPath,
                                       const std::string &modelPath)
    : atomic_code_(atomicCode), cfg_path_(jsonPath), model_path_(modelPath) {}

Sam2SegmenterUnify::~Sam2SegmenterUnify() {
    LOG_INFO("{}", "Sam2SegmenterUnify Delete");
}

util::ErrorEnum Sam2SegmenterUnify::Init() {
    if (segmenter_) {
        LOG_WARN("{}", "Init SDK SAM2 Segmenter Failed. Already Init");
        return util::ErrorEnum::Created;
    }

    try {
        // SAM2 contains encoder+decoder; current SDK does not support chaining, use single model for now
        segmenter_ = std::make_unique<cosmo::nn::DefaultComponent>(cfg_path_, model_path_, GetDeviceType(),
                                                                   &profiler_);
    } catch (const std::exception &e) {
        LOG_ERRO("Init SDK SAM2 Segmenter Failed. CfgPath:{} ModelPath:{}, {}", cfg_path_, model_path_,
                 e.what());
        segmenter_.reset();
        return util::ErrorEnum::Failed;
    }

    if (!segmenter_) {
        LOG_ERRO("Init SDK SAM2 Segmenter Failed. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);
        return util::ErrorEnum::Failed;
    }
    LOG_INFO("Init SDK SAM2 Segmenter Ok. CfgPath:{} ModelPath:{}", cfg_path_, model_path_);

    max_batch_size_ = static_cast<size_t>(segmenter_->GetMaxBatchSize());

    return util::ErrorEnum::Success;
}

util::ErrorEnum Sam2SegmenterUnify::Segment(const std::vector<VideoFramePtr> &images,
                                            const std::vector<Sam2PromptInput> &prompts,
                                            std::vector<AiDetectRstEl> &results) {
    auto start = std::chrono::high_resolution_clock::now();
    if (!segmenter_) {
        LOG_WARN("{}", "SDK SAM2 Segmenter Not Init");
        return util::ErrorEnum::NotInit;
    }

    if (images.size() != prompts.size()) {
        LOG_ERRO("SAM2 images size({}) != prompts size({})", images.size(), prompts.size());
        return util::ErrorEnum::InvalidParam;
    }

    size_t image_num = images.size();
    std::vector<VideoFramePtr> inputs;
    std::vector<Sam2PromptInput> input_prompts;

    for (size_t i = 0; i < image_num; i++) {
        inputs.push_back(images[i]);
        input_prompts.push_back(prompts[i]);
        size_t input_size = inputs.size();
        if (input_size == max_batch_size_ || (i + 1) == image_num) {
            std::vector<AiDetectRstEl> outputs;
            auto ret = Forward(inputs, input_prompts, outputs);
            if (util::ErrorEnum::Success != ret) {
                LOG_ERRO("SAM2 Forward Failed. Ret:{}", ret);
                return ret;
            }
            std::copy(outputs.begin(), outputs.end(), std::back_inserter(results));
            inputs.clear();
            input_prompts.clear();
        }
    }
    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    LOG_DEBUG("SAM2 Segment execution time: {} ms, imageNum: {}", duration.count(), image_num);
    return util::ErrorEnum::Success;
}

util::ErrorEnum Sam2SegmenterUnify::Forward(const std::vector<VideoFramePtr> &images,
                                            const std::vector<Sam2PromptInput> &prompts,
                                            std::vector<AiDetectRstEl> &results) {
    std::vector<std::shared_ptr<cosmo::nn::Blob>> image_blobs{};
    auto ret = ConvertImagesToBlobs(images, image_blobs);
    if (util::ErrorEnum::Success != ret) {
        LOG_ERRO("ConvertImagesToBlobs Failed. Ret:{}", ret);
        return ret;
    }

    // SAM2 requires 5 input groups (consistent with sophon_simple_graph demo):
    // images, point_coords, point_labels, mask_input, has_mask_input
    constexpr float kEncoderSize = 1024.f;
    constexpr int kMaxPoints     = 6;

    std::vector<std::shared_ptr<cosmo::nn::Blob>> point_coords_vec;
    std::vector<std::shared_ptr<cosmo::nn::Blob>> point_labels_vec;
    std::vector<std::shared_ptr<cosmo::nn::Blob>> mask_input_vec;
    std::vector<std::shared_ptr<cosmo::nn::Blob>> has_mask_input_vec;

    // Keep data alive until Forward returns
    std::vector<std::vector<float>> point_coords_data_storage;
    std::vector<std::vector<float>> point_labels_data_storage;
    std::vector<std::vector<float>> mask_input_data_storage;
    std::vector<std::vector<float>> has_mask_input_data_storage;

    for (size_t i = 0; i < images.size(); i++) {
        int img_w = static_cast<int>(images[i]->GetWidth());
        int img_h = static_cast<int>(images[i]->GetHeight());
        if (img_w <= 0)
            img_w = 1;
        if (img_h <= 0)
            img_h = 1;
        const auto &prompt = prompts[i];

        // 1. point_coords
        if (prompt.input_type == Sam2InputType::BOX) {
            float x1 = (prompt.coords.size() >= 4) ? prompt.coords[0] : 100.f;
            float y1 = (prompt.coords.size() >= 4) ? prompt.coords[1] : 100.f;
            float x2 = (prompt.coords.size() >= 4) ? prompt.coords[2] : 200.f;
            float y2 = (prompt.coords.size() >= 4) ? prompt.coords[3] : 200.f;
            x1       = std::max(0.f, std::min(static_cast<float>(img_w - 1), x1));
            y1       = std::max(0.f, std::min(static_cast<float>(img_h - 1), y1));
            x2       = std::max(0.f, std::min(static_cast<float>(img_w - 1), x2));
            y2       = std::max(0.f, std::min(static_cast<float>(img_h - 1), y2));
            if (x1 > x2)
                std::swap(x1, x2);
            if (y1 > y2)
                std::swap(y1, y2);
            float norm_x1 = (x1 / static_cast<float>(img_w)) * kEncoderSize;
            float norm_y1 = (y1 / static_cast<float>(img_h)) * kEncoderSize;
            float norm_x2 = (x2 / static_cast<float>(img_w)) * kEncoderSize;
            float norm_y2 = (y2 / static_cast<float>(img_h)) * kEncoderSize;

            point_coords_data_storage.emplace_back(std::vector<float>{norm_x1, norm_y1, norm_x2, norm_y2, 0.f,
                                                                      0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
            cosmo::nn::BlobDesc prompt_desc;
            prompt_desc.data_type   = cosmo::nn::DataType::DATA_TYPE_FLOAT;
            prompt_desc.dims        = {1, kMaxPoints, 2};
            prompt_desc.device_type = cosmo::nn::DeviceType::DEVICE_NAIVE;
            cosmo::nn::BlobHandle prompt_handle;
            prompt_handle.base = point_coords_data_storage.back().data();
            point_coords_vec.push_back(std::make_shared<cosmo::nn::Blob>(prompt_desc, prompt_handle));

            point_labels_data_storage.emplace_back(std::vector<float>{2.f, 3.f, -1.f, -1.f, -1.f, -1.f});
        } else  // POINT
        {
            float px     = (prompt.coords.size() >= 2 && (prompt.coords[0] != 0 || prompt.coords[1] != 0))
                               ? prompt.coords[0]
                               : (static_cast<float>(img_w) / 2.f);
            float py     = (prompt.coords.size() >= 2 && (prompt.coords[0] != 0 || prompt.coords[1] != 0))
                               ? prompt.coords[1]
                               : (static_cast<float>(img_h) / 2.f);
            px           = std::max(0.f, std::min(static_cast<float>(img_w - 1), px));
            py           = std::max(0.f, std::min(static_cast<float>(img_h - 1), py));
            float norm_x = (px / static_cast<float>(img_w)) * kEncoderSize;
            float norm_y = (py / static_cast<float>(img_h)) * kEncoderSize;

            point_coords_data_storage.emplace_back(
                std::vector<float>{norm_x, norm_y, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
            cosmo::nn::BlobDesc prompt_desc;
            prompt_desc.data_type   = cosmo::nn::DataType::DATA_TYPE_FLOAT;
            prompt_desc.dims        = {1, kMaxPoints, 2};
            prompt_desc.device_type = cosmo::nn::DeviceType::DEVICE_NAIVE;
            cosmo::nn::BlobHandle prompt_handle;
            prompt_handle.base = point_coords_data_storage.back().data();
            point_coords_vec.push_back(std::make_shared<cosmo::nn::Blob>(prompt_desc, prompt_handle));

            point_labels_data_storage.emplace_back(std::vector<float>{1.f, -1.f, -1.f, -1.f, -1.f, -1.f});
        }

        // 2. point_labels
        cosmo::nn::BlobDesc label_desc;
        label_desc.data_type   = cosmo::nn::DataType::DATA_TYPE_FLOAT;
        label_desc.dims        = {1, kMaxPoints};
        label_desc.device_type = cosmo::nn::DeviceType::DEVICE_NAIVE;
        cosmo::nn::BlobHandle label_handle;
        label_handle.base = point_labels_data_storage.back().data();
        point_labels_vec.push_back(std::make_shared<cosmo::nn::Blob>(label_desc, label_handle));

        // 3. mask_input (zeros)
        mask_input_data_storage.emplace_back(1 * 1 * 256 * 256, 0.0f);
        cosmo::nn::BlobDesc mask_desc;
        mask_desc.data_type   = cosmo::nn::DataType::DATA_TYPE_FLOAT;
        mask_desc.dims        = {1, 1, 256, 256};
        mask_desc.device_type = cosmo::nn::DeviceType::DEVICE_NAIVE;
        cosmo::nn::BlobHandle mask_handle;
        mask_handle.base = mask_input_data_storage.back().data();
        mask_input_vec.push_back(std::make_shared<cosmo::nn::Blob>(mask_desc, mask_handle));

        // 4. has_mask_input (0.0)
        has_mask_input_data_storage.emplace_back(std::vector<float>{0.0f});
        cosmo::nn::BlobDesc has_mask_desc;
        has_mask_desc.data_type   = cosmo::nn::DataType::DATA_TYPE_FLOAT;
        has_mask_desc.dims        = {1};
        has_mask_desc.device_type = cosmo::nn::DeviceType::DEVICE_NAIVE;
        cosmo::nn::BlobHandle has_mask_handle;
        has_mask_handle.base = has_mask_input_data_storage.back().data();
        has_mask_input_vec.push_back(std::make_shared<cosmo::nn::Blob>(has_mask_desc, has_mask_handle));
    }

    auto status = segmenter_->Forward(
        {image_blobs, point_coords_vec, point_labels_vec, mask_input_vec, has_mask_input_vec});
    if (!bool(status)) {
        LOG_ERRO("SAM2 Forward Failed.({})", status.description());
        return util::ErrorEnum::AI_FORWARD_FAILED;
    }

    // SAM2 output is a mask (uint8_t)
    std::vector<std::vector<uint8_t>> outputs;
    status = segmenter_->ParseOutput<uint8_t>(outputs);
    if (!bool(status)) {
        LOG_ERRO("SAM2 ParseOutput Failed.({})", status.description());
        return util::ErrorEnum::AI_PARSE_OUTPUT_FAILED;
    }

    if (results.empty()) {
        results.assign(outputs.size(), AiDetectRstEl());
    }

    for (size_t i = 0; i < outputs.size() && i < images.size(); i++) {
        auto mask                 = outputs.at(i);
        results.at(i).mask.data   = mask;
        results.at(i).mask.width  = static_cast<int>(images.at(i)->GetWidth());
        results.at(i).mask.height = static_cast<int>(images.at(i)->GetHeight());
    }
    // Inference result log: verify invocation
    for (size_t i = 0; i < results.size(); i++) {
        LOG_DEBUG("[Sam2Segment] Forward done. imageIdx:{} maskSize:{}x{} maskDataLen:{}", i,
                 results.at(i).mask.width, results.at(i).mask.height,
                 results.at(i).mask.data.empty() ? 0 : results.at(i).mask.data.size());
    }
    LOG_DEBUG("[Sam2Segment] Forward done. images:{} results:{}", images.size(), results.size());
    return util::ErrorEnum::Success;
}

util::ErrorEnum Sam2SegmenterUnify::GetMaxBatchSize(size_t &value) const {
    if (!segmenter_) {
        LOG_WARN("{}", "SDK SAM2 Segmenter Not Init");
        return util::ErrorEnum::NotInit;
    }
    value = static_cast<size_t>(segmenter_->GetMaxBatchSize());
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
