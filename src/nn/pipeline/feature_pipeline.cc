#include "nn/pipeline/feature_pipeline.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <vector>

#include "nn/pipeline/feature_output.h"
#include "nn/pipeline/pipeline_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

Status FeaturePipeline::Init(const PipelineConfig& config, const std::string& model_path,
                             DeviceType device_type, int device_id, IProfiler* profiler,
                             const std::string& tokenizer_path, const std::string& word_table_path) {
    model_info_.algorithmcode = config.algorithm_code;
    allow_fixed_padding_      = device_type == DEVICE_SOPHON_TPU;
    expected_batch_           = 0;
    output_ready_             = false;
    model_info_.reduce        = config.reduce.empty() ? "concat" : config.reduce;
    model_info_.type          = "feature";

    for (const auto& mc : config.models) {
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        std::vector<std::unique_ptr<Op>> preprocess;
        bool use_affine = false;
        if (p.contains("use_affine_crop")) {
            use_affine = pipeline_utils::ReadBool(p, "use_affine_crop", false);
        } else if (pipeline_utils::ReadBool(p, "crop", false)) {
            use_affine = false;
        } else {
            use_affine = true;
        }

        if (use_affine) {
            float norm_ratio              = pipeline_utils::ReadFloat(p, "norm_ratio", 0.4f);
            int norm_mode                 = pipeline_utils::ReadInt(p, "norm_mode", 1);
            std::vector<int> output_hw    = pipeline_utils::ReadIntArray(p, "output_hw", {112, 112}, 2);
            std::vector<int> center_index = pipeline_utils::ReadIntArray(p, "center_index", {0, 1}, 1);
            preprocess.push_back(
                pipeline_utils::MakeAffineCropOp(norm_ratio, norm_mode, output_hw, center_index));
        } else if (pipeline_utils::ReadBool(p, "crop", false)) {
            std::vector<int> dsize = pipeline_utils::ReadIntArray(p, "input_size", {112, 112}, 2);
            int gravity =
                pipeline_utils::ReadInt(p, "gravity", pipeline_utils::ReadInt(p, "padding_gravity", 0));
            std::vector<int> color = pipeline_utils::ReadIntArray(p, "padding_color", {114, 114, 114}, 1);

            float top    = pipeline_utils::ReadFloat(p, "crop_h_top", 0.0f);
            float bottom = pipeline_utils::ReadFloat(p, "crop_h_bottom", 0.0f);
            float left   = pipeline_utils::ReadFloat(p, "crop_w_left", 0.0f);
            float right  = pipeline_utils::ReadFloat(p, "crop_w_right", 0.0f);

            std::vector<float> h_top    = {top};
            std::vector<float> h_bottom = {bottom};
            std::vector<float> w_left   = {left};
            std::vector<float> w_right  = {right};

            bool square     = pipeline_utils::ReadBool(p, "square", false);
            int square_mode = pipeline_utils::ReadInt(p, "square_mode", 0);

            preprocess.push_back(pipeline_utils::MakeCropResizeOp(
                "crop", h_top, h_bottom, w_left, w_right, square, square_mode, dsize, gravity, color));
        } else {
            std::vector<int> dsize = pipeline_utils::ReadIntArray(p, "input_size", {112, 112}, 2);
            int gravity =
                pipeline_utils::ReadInt(p, "gravity", pipeline_utils::ReadInt(p, "padding_gravity", 0));
            std::vector<int> color = pipeline_utils::ReadIntArray(p, "padding_color", {114, 114, 114}, 1);
            preprocess.push_back(pipeline_utils::MakeResizeOp(dsize, gravity, color));
        }

        std::vector<float> mean = pipeline_utils::ReadFloatArray(p, "normalize_mean", {0.f, 0.f, 0.f}, 3);
        float scale             = pipeline_utils::ReadFloat(p, "normalize_scale", 1.f);
        bool is_bgr             = pipeline_utils::ReadBool(p, "is_bgr", true);
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

    nlohmann::json extra_cfg = pipeline_utils::ParseJsonObject(config.extra_config_json);
    if (extra_cfg.contains("feature_info") && extra_cfg["feature_info"].is_object()) {
        const auto& fi = extra_cfg["feature_info"];
        model_info_.config.face_info.testset_name =
            pipeline_utils::ReadString(fi, "testset_name", std::string());
        model_info_.config.face_info.score_level = pipeline_utils::ReadFloatArray(fi, "score_level", {});
        model_info_.config.face_info.cmp_score   = pipeline_utils::ReadFloatArray(fi, "cmp_score", {});
        model_info_.config.face_info.feature_dim = pipeline_utils::ReadSize(fi, "feature_dim", 512);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path);
}

Status FeaturePipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    // AiRecognizerUnify passes { imageBlobs, landmarkBlobs } once. For fused feature graphs
    // (reduce=concat/mean) each sub-model's affine_crop is a first_calculate_node with 2 inputs,
    // so the graph expects 2 * N param groups. Repeat the same image + aux for each branch.
    const size_t n_groups   = inputs.size();
    const size_t n_models   = num_models_;
    const std::string& red  = model_info_.reduce;
    const bool multi_branch = (red == "concat" || red == "mean");

    expected_batch_ = 0;
    output_ready_   = false;
    size_t expected = 0;
    if (n_groups == 2) {
        const auto& aux = *(inputs.begin() + 1);
        for (const auto& blob : aux) {
            if (!blob || blob->GetBlobDesc().dims.empty() || blob->GetBlobDesc().dims[0] <= 0)
                return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid feature ROI input batch");
            const size_t rows = static_cast<size_t>(blob->GetBlobDesc().dims[0]);
            if (max_batch_ <= 0 || rows > static_cast<size_t>(max_batch_) - expected)
                return Status(COSMO_NN_ERR_INVALID_INPUT, "Feature ROI input exceeds max batch");
            expected += rows;
        }
        if (expected == 0)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Empty feature ROI input batch");
    }

    if (n_models > 1 && n_groups == 2 && multi_branch) {
        auto it            = inputs.begin();
        const auto& images = *it;
        const auto& aux    = *(++it);
        std::vector<std::vector<std::shared_ptr<Blob>>> expanded;
        expanded.reserve(n_models * 2);
        for (size_t i = 0; i < n_models; ++i) {
            expanded.push_back(images);
            expanded.push_back(aux);
        }
        auto status = RunGraph(expanded);
        if (bool(status)) {
            expected_batch_ = expected;
            output_ready_   = true;
        }
        return status;
    }

    auto status = RunGraph(inputs);
    if (bool(status)) {
        expected_batch_ = expected;
        output_ready_   = true;
    }
    return status;
}

Status ParseFeatureOutputBlobs(const std::vector<std::shared_ptr<Blob>>& blobs,
                               std::vector<std::vector<float>>& outputs, size_t expected_batch,
                               size_t max_batch, bool allow_fixed_padding) {
    outputs.clear();
    if (blobs.size() != 1 || !blobs[0])
        return Status(COSMO_NN_ERR_NET, "Feature model output size should be 1");

    const auto& output_desc = blobs[0]->GetBlobDesc();
    if (output_desc.data_type != DATA_TYPE_FLOAT || output_desc.dims.size() != 2 ||
        output_desc.dims[0] <= 0 || output_desc.dims[1] <= 0 || DimsVectorUtils::Count(output_desc.dims) <= 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid feature output shape or type");

    const auto output_handle = blobs[0]->GetHandle();
    if (!output_handle.base)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Feature output buffer is null");
    const auto* data            = static_cast<const float*>(output_handle.base);
    const size_t physical_batch = static_cast<size_t>(output_desc.dims[0]);
    if (expected_batch != 0 && physical_batch != expected_batch &&
        !(allow_fixed_padding && expected_batch < max_batch && physical_batch == max_batch))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Feature output count does not match ROI count");
    const size_t batch    = expected_batch == 0 ? physical_batch : expected_batch;
    const int feature_len = output_desc.dims[1];
    std::vector<std::vector<float>> staged;
    staged.reserve(batch);
    for (size_t i = 0; i < batch; i++) {
        const auto* b_data  = data + i * static_cast<size_t>(feature_len);
        double norm_squared = 0;
        for (int j = 0; j < feature_len; j++) {
            if (!std::isfinite(b_data[j]))
                return Status(COSMO_NN_ERR_INVALID_INPUT, "Nonfinite feature output");
            const double value = b_data[j];
            norm_squared += value * value;
        }
        if (!std::isfinite(norm_squared) || norm_squared <= 0)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid feature norm");
        const double norm = std::sqrt(norm_squared);
        std::vector<float> feature;
        feature.reserve(static_cast<size_t>(feature_len));
        for (int j = 0; j < feature_len; j++)
            feature.push_back(static_cast<float>(b_data[j] / norm));
        staged.push_back(std::move(feature));
    }
    outputs = std::move(staged);
    return Status{COSMO_NN_OK};
}

Status FeaturePipeline::ParseFeatureOutput(std::vector<std::vector<float>>& outputs) {
    if (!output_ready_) {
        outputs.clear();
        return Status(COSMO_NN_ERR_INVALID_INPUT, "No successful feature inference output");
    }
    return ParseFeatureOutputBlobs(GetGraphOutput(), outputs, expected_batch_,
                                   max_batch_ > 0 ? static_cast<size_t>(max_batch_) : 0,
                                   allow_fixed_padding_);
}

REGISTER_MODEL_PIPELINE("feature", FeaturePipeline);

}  // namespace cosmo::nn
