#include "nn/pipeline/detection_pipeline.h"

#include <cstdio>

#include "json/json.h"
#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {

// ─── Internal Helpers ─────────────────────────────────────────

static Json::Value SafeParams(const PipelineModelConfig& mc) {
    if (mc.params_json.empty())
        return Json::Value(Json::objectValue);
    Json::Reader reader;
    Json::Value val;
    if (!reader.parse(mc.params_json, val))
        return Json::Value(Json::objectValue);
    return val;
}

static std::vector<std::unique_ptr<Op>> MakeDetPreprocess(const Json::Value& p) {
    std::vector<std::unique_ptr<Op>> ops;

    std::vector<int> dsize = {640, 640};
    if (p.isMember("input_size") && p["input_size"].isArray()) {
        dsize.clear();
        for (unsigned i = 0; i < p["input_size"].size(); i++)
            dsize.push_back(p["input_size"][i].asInt());
    }

    int gravity            = p.get("gravity", p.get("padding_gravity", 0).asInt()).asInt();
    std::vector<int> color = {114, 114, 114};
    if (p.isMember("padding_color") && p["padding_color"].isArray()) {
        color.clear();
        for (unsigned i = 0; i < p["padding_color"].size(); i++)
            color.push_back(p["padding_color"][i].asInt());
    }

    ops.push_back(pipeline_utils::MakeResizeOp(dsize, gravity, color));

    std::vector<float> mean = {0.f, 0.f, 0.f};
    if (p.isMember("normalize_mean") && p["normalize_mean"].isArray()) {
        mean.clear();
        for (unsigned i = 0; i < p["normalize_mean"].size(); i++)
            mean.push_back(p["normalize_mean"][i].asFloat());
    }
    float scale = p.get("normalize_scale", 0.00392157f).asFloat();
    bool is_bgr = p.get("is_bgr", true).asBool();

    std::vector<float> std_dev;
    if (p.isMember("normalize_std") && p["normalize_std"].isArray()) {
        for (unsigned i = 0; i < p["normalize_std"].size(); i++)
            std_dev.push_back(p["normalize_std"][i].asFloat());
    }

    ops.push_back(pipeline_utils::MakeNormalizeOp(mean, scale, is_bgr, std_dev));
    return ops;
}

static void BuildLabels(const PipelineConfig& config, const std::string& output_node,
                        const DimsVector& output_shape, CombinedModelInfo& info) {
    pipeline_utils::BuildInstructionsFromLabels(config.labels, output_node, output_shape, info.config);
}

// ─── Detection Forward Common Logic ──────────────────────

static Status DetectionForward(
    ModelPipeline* self, std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs,
    std::vector<Size>& image_sizes,
    std::function<Status(std::initializer_list<std::vector<std::shared_ptr<Blob>>>)> run_graph) {
    RETURN_ON_FAIL(run_graph(inputs));

    auto iter = inputs.begin();
    image_sizes.clear();
    for (size_t i = 0; i < iter->size(); i++) {
        auto dims   = iter->at(i)->GetBlobDesc().dims;
        auto layout = iter->at(i)->GetBlobDesc().data_format;
        Size size;
        RETURN_ON_FAIL(NetUtils::GetImageSize(dims, layout, size));
        image_sizes.push_back(size);
    }
    return COSMO_NN_OK;
}

static Status DetectionParseOutput(std::vector<std::shared_ptr<Blob>> output_blobs,
                                   const std::vector<Size>& image_sizes, const Size& net_input_size,
                                   const std::vector<int>& indices, const std::vector<float>& thresholds,
                                   const std::vector<std::string>& classnames,
                                   std::vector<std::vector<ObjectInfoV1>>& outputs) {
    outputs.clear();
    std::vector<Size> image_sizes_copy       = image_sizes;
    std::vector<int> indices_copy            = indices;
    std::vector<float> thresholds_copy       = thresholds;
    std::vector<std::string> classnames_copy = classnames;
    return NetUtils::ParseDetectionOutput(output_blobs, image_sizes_copy, net_input_size, indices_copy,
                                          thresholds_copy, classnames_copy, outputs);
}

// ============================= YOLOv5 =====================================

Status YoloV5DetPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                               DeviceType device_type, int device_id, IProfiler* profiler,
                               const std::string& tokenizer_path, const std::string& word_table_path,
                               bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "yolov5_det";

    for (auto& mc : config.models) {
        Json::Value p = SafeParams(mc);
        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        for (auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops       = MakeDetPreprocess(p);
            model.input_node_infos.push_back(std::move(input));
        }

        float nms_thresh  = p.get("nms_threshold", 0.35f).asFloat();
        float conf_thresh = p.get("confidence_threshold", 0.1f).asFloat();
        int top_k         = p.get("top_k", 1000).asInt();
        bool use_npu_post = p.get("use_npu_postprocess", false).asBool();

        // Extract input size for coordinate denormalization
        int v5_input_w = 640, v5_input_h = 640;
        if (p.isMember("input_size") && p["input_size"].isArray() && p["input_size"].size() >= 2) {
            v5_input_w = p["input_size"][0].asInt();
            v5_input_h = p["input_size"][1].asInt();
        }

        for (size_t i = 0; i < mc.outputs.size(); i++) {
            auto& out_def = mc.outputs[i];
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;

            if (i == 0) {
                if (use_npu_post) {
                    std::vector<std::vector<std::vector<float>>> anchors;
                    std::vector<float> stride;
                    if (p.isMember("anchors") && p["anchors"].isArray()) {
                        for (unsigned a = 0; a < p["anchors"].size(); a++) {
                            std::vector<std::vector<float>> grid;
                            for (unsigned b = 0; b < p["anchors"][a].size(); b++) {
                                std::vector<float> anchor;
                                for (unsigned c = 0; c < p["anchors"][a][b].size(); c++)
                                    anchor.push_back(p["anchors"][a][b][c].asFloat());
                                grid.push_back(anchor);
                            }
                            anchors.push_back(grid);
                        }
                    }
                    if (p.isMember("stride") && p["stride"].isArray())
                        for (unsigned s = 0; s < p["stride"].size(); s++)
                            stride.push_back(p["stride"][s].asFloat());
                    output.op =
                        pipeline_utils::MakeYoloNpuPostOp(nms_thresh, conf_thresh, top_k, anchors, stride);
                } else {
                    output.op = pipeline_utils::MakeYoloPostOp(nms_thresh, conf_thresh, top_k, v5_input_w,
                                                               v5_input_h);
                }
            }
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    if (!config.labels.empty() && !model_info_.models.empty()) {
        auto& last           = model_info_.models.back();
        std::string out_name = "output";
        DimsVector out_shape = {-1, -1, 6};
        if (!last.output_node_infos.empty())
            out_name = last.output_node_infos.front().name;
        BuildLabels(config, out_name, out_shape, model_info_);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status YoloV5DetPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    return DetectionForward(
        this, inputs, image_sizes_,
        [this](std::initializer_list<std::vector<std::shared_ptr<Blob>>> inp) { return RunGraph(inp); });
}

Status YoloV5DetPipeline::ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return DetectionParseOutput(GetGraphOutput(), image_sizes_, net_input_size_, selected_indices_,
                                selected_thresholds_, selected_classnames_, outputs);
}

// ============================= YOLOv8 =====================================

Status YoloV8DetPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                               DeviceType device_type, int device_id, IProfiler* profiler,
                               const std::string& tokenizer_path, const std::string& word_table_path,
                               bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "yolov8_det";

    for (auto& mc : config.models) {
        Json::Value p = SafeParams(mc);
        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        for (auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops       = MakeDetPreprocess(p);
            model.input_node_infos.push_back(std::move(input));
        }

        float nms_thresh  = p.get("nms_threshold", 0.7f).asFloat();
        float conf_thresh = p.get("confidence_threshold", 0.25f).asFloat();
        int top_k         = p.get("top_k", 300).asInt();

        // Extract input size for coordinate denormalization
        int post_input_w = 640, post_input_h = 640;
        if (p.isMember("input_size") && p["input_size"].isArray() && p["input_size"].size() >= 2) {
            post_input_w = p["input_size"][0].asInt();
            post_input_h = p["input_size"][1].asInt();
        }

        for (size_t i = 0; i < mc.outputs.size(); i++) {
            auto& out_def = mc.outputs[i];
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            if (i == 0)
                output.op = pipeline_utils::MakeYoloV8PostOp(nms_thresh, conf_thresh, top_k, post_input_w,
                                                             post_input_h);
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    if (!config.labels.empty() && !model_info_.models.empty()) {
        auto& last           = model_info_.models.back();
        std::string out_name = "output0";
        DimsVector out_shape = {-1, -1, 6};
        if (!last.output_node_infos.empty())
            out_name = last.output_node_infos.front().name;
        BuildLabels(config, out_name, out_shape, model_info_);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status YoloV8DetPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    return DetectionForward(
        this, inputs, image_sizes_,
        [this](std::initializer_list<std::vector<std::shared_ptr<Blob>>> inp) { return RunGraph(inp); });
}

Status YoloV8DetPipeline::ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return DetectionParseOutput(GetGraphOutput(), image_sizes_, net_input_size_, selected_indices_,
                                selected_thresholds_, selected_classnames_, outputs);
}

// ============================= YOLO26 (End-to-End) =======================

Status Yolo26DetPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                               DeviceType device_type, int device_id, IProfiler* profiler,
                               const std::string& tokenizer_path, const std::string& word_table_path,
                               bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "yolo26_det";

    for (auto& mc : config.models) {
        Json::Value p = SafeParams(mc);
        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        for (auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops       = MakeDetPreprocess(p);
            model.input_node_infos.push_back(std::move(input));
        }

        float conf_thresh = p.get("confidence_threshold", 0.25f).asFloat();
        int top_k         = p.get("top_k", 300).asInt();

        // Extract input size for coordinate denormalization
        int e2e_input_w = 640, e2e_input_h = 640;
        if (p.isMember("input_size") && p["input_size"].isArray() && p["input_size"].size() >= 2) {
            e2e_input_w = p["input_size"][0].asInt();
            e2e_input_h = p["input_size"][1].asInt();
        }

        for (size_t i = 0; i < mc.outputs.size(); i++) {
            auto& out_def = mc.outputs[i];
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;
            if (i == 0)
                output.op = pipeline_utils::MakeYoloE2EPostOp(conf_thresh, top_k, e2e_input_w, e2e_input_h);
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    if (!config.labels.empty() && !model_info_.models.empty()) {
        auto& last           = model_info_.models.back();
        std::string out_name = "output";
        DimsVector out_shape = {-1, -1, 6};
        if (!last.output_node_infos.empty())
            out_name = last.output_node_infos.front().name;
        BuildLabels(config, out_name, out_shape, model_info_);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status Yolo26DetPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    return DetectionForward(
        this, inputs, image_sizes_,
        [this](std::initializer_list<std::vector<std::shared_ptr<Blob>>> inp) { return RunGraph(inp); });
}

Status Yolo26DetPipeline::ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return DetectionParseOutput(GetGraphOutput(), image_sizes_, net_input_size_, selected_indices_,
                                selected_thresholds_, selected_classnames_, outputs);
}

// ========================= Generic Detector ===============================

Status GenericDetectorPipeline::Init(const PipelineConfig& config, const std::string& model_path,
                                     DeviceType device_type, int device_id, IProfiler* profiler,
                                     const std::string& tokenizer_path, const std::string& word_table_path,
                                     bool use_skip) {
    model_info_.algorithmcode = config.algorithm_code;
    model_info_.reduce        = config.reduce;
    model_info_.type          = "detector";

    for (auto& mc : config.models) {
        Json::Value p = SafeParams(mc);
        ModelInfo model;
        model.name      = mc.name;
        model.filename  = mc.file_name;
        model.file_md5  = mc.file_md5;
        model.max_batch = mc.max_batch;
        max_batch_      = mc.max_batch;

        for (auto& in_def : mc.inputs) {
            InputNodeInfo input;
            input.name      = in_def.name;
            input.shape     = in_def.shape;
            input.data_type = in_def.data_type;
            input.ops       = MakeDetPreprocess(p);
            model.input_node_infos.push_back(std::move(input));
        }

        std::string post_type = p.get("post_type", "yolo").asString();
        float nms_thresh      = p.get("nms_threshold", 0.35f).asFloat();
        float conf_thresh     = p.get("confidence_threshold", 0.1f).asFloat();
        int top_k             = p.get("top_k", 1000).asInt();

        for (size_t i = 0; i < mc.outputs.size(); i++) {
            auto& out_def = mc.outputs[i];
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;

            if (i == 0) {
                // Extract input size for coordinate denormalization
                int gd_input_w = 640, gd_input_h = 640;
                if (p.isMember("input_size") && p["input_size"].isArray() && p["input_size"].size() >= 2) {
                    gd_input_w = p["input_size"][0].asInt();
                    gd_input_h = p["input_size"][1].asInt();
                }
                if (post_type == "yolov8") {
                    output.op = pipeline_utils::MakeYoloV8PostOp(nms_thresh, conf_thresh, top_k, gd_input_w,
                                                                 gd_input_h);
                } else if (post_type == "yolo_e2e") {
                    output.op = pipeline_utils::MakeYoloE2EPostOp(conf_thresh, top_k, gd_input_w, gd_input_h);
                } else if (post_type == "yolo_npu") {
                    std::vector<std::vector<std::vector<float>>> anchors;
                    std::vector<float> stride;
                    if (p.isMember("anchors") && p["anchors"].isArray())
                        for (unsigned a = 0; a < p["anchors"].size(); a++) {
                            std::vector<std::vector<float>> grid;
                            for (unsigned b = 0; b < p["anchors"][a].size(); b++) {
                                std::vector<float> anchor;
                                for (unsigned c = 0; c < p["anchors"][a][b].size(); c++)
                                    anchor.push_back(p["anchors"][a][b][c].asFloat());
                                grid.push_back(anchor);
                            }
                            anchors.push_back(grid);
                        }
                    if (p.isMember("stride") && p["stride"].isArray())
                        for (unsigned s = 0; s < p["stride"].size(); s++)
                            stride.push_back(p["stride"][s].asFloat());
                    output.op =
                        pipeline_utils::MakeYoloNpuPostOp(nms_thresh, conf_thresh, top_k, anchors, stride);
                } else {
                    output.op = pipeline_utils::MakeYoloPostOp(nms_thresh, conf_thresh, top_k, gd_input_w,
                                                               gd_input_h);
                }
            }
            model.output_node_infos.push_back(std::move(output));
        }
        model_info_.models.push_back(std::move(model));
    }

    if (!config.labels.empty() && !model_info_.models.empty()) {
        auto& last           = model_info_.models.back();
        std::string out_name = "output";
        DimsVector out_shape = {-1, -1, 6};
        if (!last.output_node_infos.empty())
            out_name = last.output_node_infos.front().name;
        BuildLabels(config, out_name, out_shape, model_info_);
    }

    InitThresholdsAndLabels();
    InitNetInputSize();
    return InitGraph(model_path, device_type, device_id, profiler, tokenizer_path, use_skip);
}

Status GenericDetectorPipeline::Forward(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs) {
    return DetectionForward(
        this, inputs, image_sizes_,
        [this](std::initializer_list<std::vector<std::shared_ptr<Blob>>> inp) { return RunGraph(inp); });
}

Status GenericDetectorPipeline::ParseDetectionOutput(std::vector<std::vector<ObjectInfoV1>>& outputs) {
    return DetectionParseOutput(GetGraphOutput(), image_sizes_, net_input_size_, selected_indices_,
                                selected_thresholds_, selected_classnames_, outputs);
}

// ===================== Auto-Registration ==================================

REGISTER_MODEL_PIPELINE("yolov5_det", YoloV5DetPipeline);
REGISTER_MODEL_PIPELINE("yolov8_det", YoloV8DetPipeline);
REGISTER_MODEL_PIPELINE("yolov9_det", YoloV8DetPipeline);
REGISTER_MODEL_PIPELINE("yolov11_det", YoloV8DetPipeline);
REGISTER_MODEL_PIPELINE("yolov12_det", YoloV8DetPipeline);
REGISTER_MODEL_PIPELINE("yolo26_det", Yolo26DetPipeline);
REGISTER_MODEL_PIPELINE("detector", GenericDetectorPipeline);

}  // namespace cosmo::nn
