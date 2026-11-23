#include "nn/pipeline/keypoints_pipeline.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>

#include "nn/pipeline/keypoints_input.h"
#include "nn/pipeline/pipeline_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

Status KeypointsPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                               DeviceType device_type, int device_id, IProfiler* profiler,
                               const std::string& tokenizer_path, const std::string& word_table_path) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "keypoints";
    allow_fixed_padding_      = device_type == DEVICE_SOPHON_TPU;
    output_ready_             = false;
    declared_output_shape_.clear();

    // ParseKeypointsOutput consumes exactly one model output. Its configured
    // coordinate width is required to distinguish, for example, five-point
    // face landmarks from four-point OCR geometry.
    if (config.models.size() != 1 || config.models.front().outputs.size() != 1)
        return Status(COSMO_NN_ERR_INVALID_CFG, "Keypoints require one declared model output");
    declared_output_shape_ = config.models.front().outputs.front().shape;
    RETURN_ON_FAIL(ValidateKeypointOutputColumns(declared_output_shape_, declared_output_shape_));

    for (const auto& mc : config.models) {
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        std::vector<int> dsize = pipeline_utils::ReadIntArray(p, "input_size", {48, 48}, 2);

        std::vector<float> mean =
            pipeline_utils::ReadFloatArray(p, "normalize_mean", {127.5f, 127.5f, 127.5f}, 3);
        float scale = pipeline_utils::ReadFloat(p, "normalize_scale", 0.0078125f);
        bool is_bgr = pipeline_utils::ReadBool(p, "is_bgr", true);

        bool has_crop = pipeline_utils::ReadBool(p, "crop", false);
        std::vector<std::unique_ptr<Op>> preprocess;
        if (has_crop) {
            float top    = pipeline_utils::ReadFloat(p, "crop_h_top", 0.0f);
            float bottom = pipeline_utils::ReadFloat(p, "crop_h_bottom", 0.0f);
            float left   = pipeline_utils::ReadFloat(p, "crop_w_left", 0.0f);
            float right  = pipeline_utils::ReadFloat(p, "crop_w_right", 0.0f);

            std::vector<float> h_top    = {top};
            std::vector<float> h_bottom = {bottom};
            std::vector<float> w_left   = {left};
            std::vector<float> w_right  = {right};

            preprocess.push_back(pipeline_utils::MakeCropResizeOp("crop", h_top, h_bottom, w_left, w_right,
                                                                  false, 0, dsize, 0, {0, 0, 0}));
        } else {
            preprocess.push_back(pipeline_utils::MakeResizeOp(dsize, 0, {0, 0, 0}));
        }
        preprocess.push_back(pipeline_utils::MakeNormalizeOp(mean, scale, is_bgr));

        for (const auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops.swap(preprocess);
            model.input_node_infos.push_back(std::move(input));
        }
        for (const auto& out_def : mc.outputs) {
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            output.op        = nullptr;
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path);
}

Status ValidateKeypointOutputColumns(const DimsVector& declared, const DimsVector& actual) {
    const auto valid = [](const DimsVector& shape) {
        return shape.size() >= 2 && shape[1] > 0 && shape[1] % 2 == 0 &&
               std::all_of(shape.begin() + 2, shape.end(), [](int dim) { return dim == 1; });
    };
    if (!valid(declared) || !valid(actual) || declared[1] != actual[1])
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Keypoints output differs from declared coordinate width");
    return Status{COSMO_NN_OK};
}

Status CollectKeypointRectangles(const std::vector<std::shared_ptr<Blob>>& blobs, size_t max_batch,
                                 std::vector<Rect>& rectangles) {
    rectangles.clear();
    if (blobs.empty() || !max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Keypoints require nonempty ROI inputs");
    std::vector<Rect> staged;
    for (const auto& blob : blobs) {
        if (!blob)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Keypoints ROI blob is null");
        const auto& desc = blob->GetBlobDesc();
        if (desc.data_type != DATA_TYPE_INT32 || desc.dims.size() != 2 || desc.dims[0] <= 0 ||
            desc.dims[1] != 4 || DimsVectorUtils::Count(desc.dims) <= 0)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid keypoints ROI shape or type");
        const auto* data = static_cast<const unsigned char*>(blob->GetHandle().base);
        const auto count = static_cast<size_t>(desc.dims[0]);
        if (!data || staged.size() > max_batch || count > max_batch - staged.size())
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid keypoints ROI buffer or batch size");
        for (size_t i = 0; i < count; ++i) {
            int32_t values[4];
            std::memcpy(values, data + i * sizeof(values), sizeof(values));
            if (values[2] <= 0 || values[3] <= 0)
                return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid keypoints ROI dimensions");
            staged.emplace_back(values[0], values[1], values[2], values[3]);
        }
    }
    rectangles = std::move(staged);
    return Status{COSMO_NN_OK};
}

Status KeypointsPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    output_ready_ = false;
    rects_.clear();
    if (inputs.size() != 2 || inputs.begin()->empty() ||
        inputs.begin()->size() != (inputs.begin() + 1)->size() || max_batch_ <= 0 ||
        !std::all_of(inputs.begin()->begin(), inputs.begin()->end(),
                     [](const auto& image) { return bool(image); }))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Keypoints require matching image and ROI input groups");
    std::vector<Rect> staged;
    RETURN_ON_FAIL(CollectKeypointRectangles(*(inputs.begin() + 1), static_cast<size_t>(max_batch_), staged));
    RETURN_ON_FAIL(RunGraph(inputs));
    rects_        = std::move(staged);
    output_ready_ = true;
    return Status{COSMO_NN_OK};
}

Status KeypointsPipeline::ParseKeypointsOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    outputs.clear();
    if (!output_ready_)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No successful keypoints inference output");
    std::vector<std::shared_ptr<Blob>> output_blobs = GetGraphOutput();
    if (output_blobs.size() != 1 || !output_blobs[0])
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Keypoints require a single output tensor");
    RETURN_ON_FAIL(
        ValidateKeypointOutputColumns(declared_output_shape_, output_blobs[0]->GetBlobDesc().dims));
    return NetUtils::ParseKeypointOutput(output_blobs, rects_, outputs,
                                         max_batch_ > 0 ? static_cast<size_t>(max_batch_) : 0,
                                         allow_fixed_padding_);
}

REGISTER_MODEL_PIPELINE("keypoints", KeypointsPipeline);

}  // namespace cosmo::nn
