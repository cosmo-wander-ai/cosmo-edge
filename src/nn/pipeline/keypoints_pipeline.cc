#include "nn/pipeline/keypoints_pipeline.h"

#include "json/json.h"
#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {

Status KeypointsPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                               DeviceType device_type, int device_id, IProfiler* profiler,
                               const std::string& tokenizer_path, const std::string& word_table_path,
                               bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "keypoints";

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

        std::vector<int> dsize = {48, 48};
        if (p.isMember("input_size") && p["input_size"].isArray()) {
            dsize.clear();
            for (unsigned i = 0; i < p["input_size"].size(); i++)
                dsize.push_back(p["input_size"][i].asInt());
        }

        std::vector<float> mean = {127.5f, 127.5f, 127.5f};
        if (p.isMember("normalize_mean") && p["normalize_mean"].isArray()) {
            mean.clear();
            for (unsigned i = 0; i < p["normalize_mean"].size(); i++)
                mean.push_back(p["normalize_mean"][i].asFloat());
        }
        float scale = p.get("normalize_scale", 0.0078125f).asFloat();
        bool is_bgr = p.get("is_bgr", true).asBool();

        bool has_crop = p.get("crop", false).asBool();
        std::vector<std::unique_ptr<Op>> preprocess;
        if (has_crop) {
            float top    = p.get("crop_h_top", 0.0f).asFloat();
            float bottom = p.get("crop_h_bottom", 0.0f).asFloat();
            float left   = p.get("crop_w_left", 0.0f).asFloat();
            float right  = p.get("crop_w_right", 0.0f).asFloat();

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

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status KeypointsPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    if (inputs.size() != 2)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "keypoints model requires 2 inputs");

    RETURN_ON_FAIL(RunGraph(inputs));

    auto iter = inputs.begin();
    iter++;

    rects_.clear();
    for (size_t i = 0; i < iter->size(); i++) {
        auto blob     = iter->at(i);
        auto dims     = blob->GetBlobDesc().dims;
        const int n   = dims.at(0);
        int32_t* data = (int32_t*)blob->GetHandle().base;
        for (int j = 0; j < n; j++) {
            Rect2i r;
            r.x      = data[j * 4 + 0];
            r.y      = data[j * 4 + 1];
            r.width  = data[j * 4 + 2];
            r.height = data[j * 4 + 3];
            rects_.push_back(r);
        }
    }
    return COSMO_NN_OK;
}

Status KeypointsPipeline::ParseKeypointsOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    outputs.clear();
    std::vector<std::shared_ptr<Blob>> output_blobs = GetGraphOutput();
    return NetUtils::ParseKeypointOutput(output_blobs, rects_, outputs);
}

REGISTER_MODEL_PIPELINE("keypoints", KeypointsPipeline);

}  // namespace cosmo::nn
