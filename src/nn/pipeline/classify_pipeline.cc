#include "nn/pipeline/classify_pipeline.h"

#include <nlohmann/json.hpp>

#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {

Status ClassifyPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                              DeviceType device_type, int device_id, IProfiler* profiler,
                              const std::string& tokenizer_path, const std::string& word_table_path,
                              bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "classify";

    for (auto& mc : config.models) {
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        std::vector<int> dsize = pipeline_utils::ReadIntArray(p, "input_size", {224, 224}, 2);

        int gravity = pipeline_utils::ReadInt(p, "gravity", pipeline_utils::ReadInt(p, "padding_gravity", 0));
        std::vector<int> color = pipeline_utils::ReadIntArray(p, "padding_color", {114, 114, 114}, 1);

        std::vector<float> mean =
            pipeline_utils::ReadFloatArray(p, "normalize_mean", {0.f, 0.f, 0.f}, 3);
        float scale = pipeline_utils::ReadFloat(p, "normalize_scale", 0.00392157f);
        bool is_bgr = pipeline_utils::ReadBool(p, "is_bgr", false);

        std::vector<std::unique_ptr<Op>> preprocess;
        bool has_crop = pipeline_utils::ReadBool(p, "crop", false);
        if (has_crop) {
            // Only new format supported: four directions each as a single float
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
            preprocess.push_back(pipeline_utils::MakeResizeOp(dsize, gravity, color));
        }
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

    if (!config.labels.empty() && !model_info_.models.empty()) {
        auto& last           = model_info_.models.back();
        std::string out_name = "output0";
        DimsVector out_shape;
        if (!last.output_node_infos.empty()) {
            out_name  = last.output_node_infos.front().name;
            out_shape = last.output_node_infos.front().shape;
        }
        pipeline_utils::BuildInstructionsFromLabels(config.labels, out_name, out_shape, model_info_.config);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status ClassifyPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    return RunGraph(inputs);
}

Status ClassifyPipeline::ParseClassifyOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    outputs.clear();
    std::vector<std::shared_ptr<Blob>> output_blobs = GetGraphOutput();
    return NetUtils::ParseClassificationOutput(output_blobs, selected_indices_, selected_classnames_,
                                               outputs);
}

REGISTER_MODEL_PIPELINE("classify", ClassifyPipeline);

}  // namespace cosmo::nn
