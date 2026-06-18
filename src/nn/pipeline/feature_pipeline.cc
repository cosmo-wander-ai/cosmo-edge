#include "nn/pipeline/feature_pipeline.h"

#include <cmath>
#include <vector>

#include "json/json.h"
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

        std::vector<Op*> preprocess;
        bool use_affine = false;
        if (p.isMember("use_affine_crop")) {
            use_affine = p["use_affine_crop"].asBool();
        } else if (p.isMember("crop") && p["crop"].asBool()) {
            use_affine = false;
        } else {
            use_affine = true;
        }

        if (use_affine) {
            float norm_ratio           = p.get("norm_ratio", 0.4f).asFloat();
            int norm_mode              = p.get("norm_mode", 1).asInt();
            std::vector<int> output_hw = {112, 112};
            if (p.isMember("output_hw") && p["output_hw"].isArray()) {
                output_hw.clear();
                for (unsigned i = 0; i < p["output_hw"].size(); i++)
                    output_hw.push_back(p["output_hw"][i].asInt());
            }
            std::vector<int> center_index = {0, 1};
            if (p.isMember("center_index") && p["center_index"].isArray()) {
                center_index.clear();
                for (unsigned i = 0; i < p["center_index"].size(); i++)
                    center_index.push_back(p["center_index"][i].asInt());
            }
            preprocess.push_back(
                pipeline_utils::MakeAffineCropOp(norm_ratio, norm_mode, output_hw, center_index));
        } else if (p.isMember("crop") && p["crop"].asBool()) {
            std::vector<int> dsize = {112, 112};
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
            std::vector<int> dsize = {112, 112};
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
            preprocess.push_back(pipeline_utils::MakeResizeOp(dsize, gravity, color));
        }

        std::vector<float> mean = {0.f, 0.f, 0.f};
        if (p.isMember("normalize_mean") && p["normalize_mean"].isArray()) {
            mean.clear();
            for (unsigned i = 0; i < p["normalize_mean"].size(); i++)
                mean.push_back(p["normalize_mean"][i].asFloat());
        }
        float scale = p.get("normalize_scale", 1.f).asFloat();
        bool is_bgr = p.get("is_bgr", true).asBool();
        preprocess.push_back(pipeline_utils::MakeNormalizeOp(mean, scale, is_bgr));

        for (auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops       = preprocess;
            model.input_node_infos.push_back(input);
        }
        for (auto& out_def : mc.outputs) {
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            output.op        = nullptr;
            model.output_node_infos.push_back(output);
        }
        model_info_.models.push_back(model);
    }

    Json::Value extra_cfg;
    if (!config.extra_config_json.empty()) {
        Json::Reader r;
        r.parse(config.extra_config_json, extra_cfg);
    }
    if (extra_cfg.isMember("feature_info")) {
        auto& fi                                  = extra_cfg["feature_info"];
        model_info_.config.face_info.testset_name = fi.get("testset_name", "").asString();
        if (fi.isMember("score_level") && fi["score_level"].isArray())
            for (unsigned i = 0; i < fi["score_level"].size(); i++)
                model_info_.config.face_info.score_level.push_back(fi["score_level"][i].asFloat());
        if (fi.isMember("cmp_score") && fi["cmp_score"].isArray())
            for (unsigned i = 0; i < fi["cmp_score"].size(); i++)
                model_info_.config.face_info.cmp_score.push_back(fi["cmp_score"][i].asFloat());
        model_info_.config.face_info.feature_dim = fi.get("feature_dim", 512).asUInt();
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
    const size_t n_models   = model_info_.models.size();
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
