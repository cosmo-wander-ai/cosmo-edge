#include "nn/pipeline/feature_pipeline.h"

#include <cmath>
#include <vector>

#include <nlohmann/json.hpp>

#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {

Status FeaturePipeline::Init(const PipelineConfig& config, const std::string& model_path,
                             DeviceType device_type, int device_id, IProfiler* profiler,
                             const std::string& tokenizer_path, const std::string& word_table_path,
                             bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce.empty() ? "concat" : config.reduce;
    model_info_.type          = "feature";

    for (auto& mc : config.models) {
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
            float norm_ratio = pipeline_utils::ReadFloat(p, "norm_ratio", 0.4f);
            int norm_mode    = pipeline_utils::ReadInt(p, "norm_mode", 1);
            std::vector<int> output_hw =
                pipeline_utils::ReadIntArray(p, "output_hw", {112, 112}, 2);
            std::vector<int> center_index =
                pipeline_utils::ReadIntArray(p, "center_index", {0, 1}, 1);
            preprocess.push_back(
                pipeline_utils::MakeAffineCropOp(norm_ratio, norm_mode, output_hw, center_index));
        } else if (pipeline_utils::ReadBool(p, "crop", false)) {
            std::vector<int> dsize = pipeline_utils::ReadIntArray(p, "input_size", {112, 112}, 2);
            int gravity =
                pipeline_utils::ReadInt(p, "gravity", pipeline_utils::ReadInt(p, "padding_gravity", 0));
            std::vector<int> color =
                pipeline_utils::ReadIntArray(p, "padding_color", {114, 114, 114}, 1);

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
            std::vector<int> color =
                pipeline_utils::ReadIntArray(p, "padding_color", {114, 114, 114}, 1);
            preprocess.push_back(pipeline_utils::MakeResizeOp(dsize, gravity, color));
        }

        std::vector<float> mean =
            pipeline_utils::ReadFloatArray(p, "normalize_mean", {0.f, 0.f, 0.f}, 3);
        float scale = pipeline_utils::ReadFloat(p, "normalize_scale", 1.f);
        bool is_bgr = pipeline_utils::ReadBool(p, "is_bgr", true);
        preprocess.push_back(pipeline_utils::MakeNormalizeOp(mean, scale, is_bgr));

        for (auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops       = std::move(preprocess);
            model.input_node_infos.push_back(std::move(input));
        }
        for (auto& out_def : mc.outputs) {
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
        const auto& fi                            = extra_cfg["feature_info"];
        model_info_.config.face_info.testset_name =
            pipeline_utils::ReadString(fi, "testset_name", std::string());
        model_info_.config.face_info.score_level =
            pipeline_utils::ReadFloatArray(fi, "score_level", {});
        model_info_.config.face_info.cmp_score = pipeline_utils::ReadFloatArray(fi, "cmp_score", {});
        model_info_.config.face_info.feature_dim = pipeline_utils::ReadSize(fi, "feature_dim", 512);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status FeaturePipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    // AiRecognizerUnify passes { imageBlobs, landmarkBlobs } once. For fused feature graphs
    // (reduce=concat/mean) each sub-model's affine_crop is a first_calculate_node with 2 inputs,
    // so the graph expects 2 * N param groups. Repeat the same image + aux for each branch.
    const size_t n_groups   = inputs.size();
    const size_t n_models   = num_models_;
    const std::string& red  = model_info_.reduce;
    const bool multi_branch = (red == "concat" || red == "mean");

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
        return RunGraph(expanded);
    }

    return RunGraph(inputs);
}

Status FeaturePipeline::ParseFeatureOutput(std::vector<std::vector<float>>& outputs) {
    outputs.clear();
    auto output_blobs = GetGraphOutput();
    if (output_blobs.size() != 1)
        return Status(COSMO_NN_ERR_NET, "Feature model output size should be 1");

    auto output_blob   = output_blobs.at(0);
    auto output_desc   = output_blob->GetBlobDesc();
    auto output_handle = output_blob->GetHandle();

    const int batch       = output_desc.dims.at(0);
    const int feature_len = output_desc.dims.at(1);
    float* data           = reinterpret_cast<float*>(output_handle.base);

    outputs.resize(batch);
    for (int i = 0; i < batch; i++) {
        float* b_data = data + i * feature_len;
        float norm    = 0;
        for (int j = 0; j < feature_len; j++)
            norm += b_data[j] * b_data[j];
        norm = std::sqrt(norm);
        for (int j = 0; j < feature_len; j++)
            b_data[j] /= norm;
        outputs[i] = std::vector<float>(b_data, b_data + feature_len);
    }
    return COSMO_NN_OK;
}

REGISTER_MODEL_PIPELINE("feature", FeaturePipeline);

}  // namespace cosmo::nn
