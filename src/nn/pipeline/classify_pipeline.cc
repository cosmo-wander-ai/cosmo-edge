#include "nn/pipeline/classify_pipeline.h"

#include "json/json.h"
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
        Json::Value p(Json::objectValue);
        if (!mc.params_json.empty()) {
            Json::Reader r;
            r.parse(mc.params_json, p);
        }

        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        std::vector<int> dsize = {224, 224};
        if (p.isMember("input_size") && p["input_size"].isArray()) {
            dsize.clear();
            for (unsigned i = 0; i < p["input_size"].size(); i++)
                dsize.push_back(p["input_size"][i].asInt());
        }

        int gravity            = p.get("gravity", p.get("padding_gravity", 0)).asInt();
        std::vector<int> color = {114, 114, 114};
        if (p.isMember("padding_color") && p["padding_color"].isArray()) {
            color.clear();
            for (unsigned i = 0; i < p["padding_color"].size(); i++)
                color.push_back(p["padding_color"][i].asInt());
        }

        std::vector<float> mean = {0.f, 0.f, 0.f};
        if (p.isMember("normalize_mean") && p["normalize_mean"].isArray()) {
            mean.clear();
            for (unsigned i = 0; i < p["normalize_mean"].size(); i++)
                mean.push_back(p["normalize_mean"][i].asFloat());
        }
        float scale = p.get("normalize_scale", 0.00392157f).asFloat();
        bool is_bgr = p.get("is_bgr", false).asBool();

        std::vector<std::unique_ptr<Op>> preprocess;
        bool has_crop = p.isMember("crop") && p["crop"].isBool() && p["crop"].asBool();
        if (has_crop) {
            // Only new format supported: four directions each as a single float
            float top    = p.get("crop_h_top", 0.0f).asFloat();
            float bottom = p.get("crop_h_bottom", 0.0f).asFloat();
            float left   = p.get("crop_w_left", 0.0f).asFloat();
            float right  = p.get("crop_w_right", 0.0f).asFloat();

            std::vector<float> h_top    = {top};
            std::vector<float> h_bottom = {bottom};
            std::vector<float> w_left   = {left};
            std::vector<float> w_right  = {right};

            bool square     = p.get("square", false).asBool();
            int square_mode = p.get("square_mode", 0).asInt();

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
