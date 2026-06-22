#include "nn/pipeline/detection_pipeline.h"

#include <cstdio>

#include <nlohmann/json.hpp>

#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {

// ─── Internal Helpers ─────────────────────────────────────────

static bool ReadYoloNpuPostParams(const nlohmann::json& json,
                                  std::vector<std::vector<std::vector<float>>>& anchors,
                                  std::vector<float>& stride) {
    anchors = pipeline_utils::ReadFloat3DArray(json, "anchors", {}, 1, 1, 2);
    stride  = pipeline_utils::ReadFloatArray(json, "stride", {}, 1);

    if (anchors.empty() || stride.empty() || anchors.size() != stride.size())
        return false;

    for (const auto& grid : anchors) {
        if (grid.empty())
            return false;
        for (const auto& anchor : grid) {
            if (anchor.size() < 2)
                return false;
        }
    }
    return true;
}

static std::vector<std::unique_ptr<Op>> MakeDetPreprocess(const nlohmann::json& p) {
    std::vector<std::unique_ptr<Op>> ops;

    std::vector<int> dsize = pipeline_utils::ReadIntArray(p, "input_size", {640, 640}, 2);
    int gravity = pipeline_utils::ReadInt(p, "gravity", pipeline_utils::ReadInt(p, "padding_gravity", 0));
    std::vector<int> color = pipeline_utils::ReadIntArray(p, "padding_color", {114, 114, 114}, 1);

    ops.push_back(pipeline_utils::MakeResizeOp(dsize, gravity, color));

    std::vector<float> mean = pipeline_utils::ReadFloatArray(p, "normalize_mean", {0.f, 0.f, 0.f}, 3);
    float scale  = pipeline_utils::ReadFloat(p, "normalize_scale", 0.00392157f);
    bool is_bgr  = pipeline_utils::ReadBool(p, "is_bgr", true);
    std::vector<float> std_dev = pipeline_utils::ReadFloatArray(p, "normalize_std", {}, 3);

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
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);
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

        float nms_thresh  = pipeline_utils::ReadFloat(p, "nms_threshold", 0.35f);
        float conf_thresh = pipeline_utils::ReadFloat(p, "confidence_threshold", 0.1f);
        int top_k         = pipeline_utils::ReadInt(p, "top_k", 1000);
        bool use_npu_post = pipeline_utils::ReadBool(p, "use_npu_postprocess", false);

        // Extract input size for coordinate denormalization
        int v5_input_w = 640, v5_input_h = 640;
        std::vector<int> input_size = pipeline_utils::ReadIntArray(p, "input_size", {}, 2);
        if (input_size.size() >= 2) {
            v5_input_w = input_size[0];
            v5_input_h = input_size[1];
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
                    if (!ReadYoloNpuPostParams(p, anchors, stride)) {
                        return Status(COSMO_NN_ERR_PARAM,
                                      "Invalid yolo_npu anchors/stride config");
                    }
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
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);
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

        float nms_thresh  = pipeline_utils::ReadFloat(p, "nms_threshold", 0.7f);
        float conf_thresh = pipeline_utils::ReadFloat(p, "confidence_threshold", 0.25f);
        int top_k         = pipeline_utils::ReadInt(p, "top_k", 300);

        // Extract input size for coordinate denormalization
        int post_input_w = 640, post_input_h = 640;
        std::vector<int> input_size = pipeline_utils::ReadIntArray(p, "input_size", {}, 2);
        if (input_size.size() >= 2) {
            post_input_w = input_size[0];
            post_input_h = input_size[1];
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
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);
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

        float conf_thresh = pipeline_utils::ReadFloat(p, "confidence_threshold", 0.25f);
        int top_k         = pipeline_utils::ReadInt(p, "top_k", 300);

        // Extract input size for coordinate denormalization
        int e2e_input_w = 640, e2e_input_h = 640;
        std::vector<int> input_size = pipeline_utils::ReadIntArray(p, "input_size", {}, 2);
        if (input_size.size() >= 2) {
            e2e_input_w = input_size[0];
            e2e_input_h = input_size[1];
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
        nlohmann::json p = pipeline_utils::ParseJsonObject(mc.params_json);
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

        std::string post_type = pipeline_utils::ReadString(p, "post_type", std::string("yolo"));
        float nms_thresh      = pipeline_utils::ReadFloat(p, "nms_threshold", 0.35f);
        float conf_thresh     = pipeline_utils::ReadFloat(p, "confidence_threshold", 0.1f);
        int top_k             = pipeline_utils::ReadInt(p, "top_k", 1000);

        for (size_t i = 0; i < mc.outputs.size(); i++) {
            auto& out_def = mc.outputs[i];
            OutputNodeInfo output;
            output.name      = out_def.name;
            output.shape     = out_def.shape;
            output.data_type = out_def.data_type;

            if (i == 0) {
                // Extract input size for coordinate denormalization
                int gd_input_w = 640, gd_input_h = 640;
                std::vector<int> input_size = pipeline_utils::ReadIntArray(p, "input_size", {}, 2);
                if (input_size.size() >= 2) {
                    gd_input_w = input_size[0];
                    gd_input_h = input_size[1];
                }
                if (post_type == "yolov8") {
                    output.op = pipeline_utils::MakeYoloV8PostOp(nms_thresh, conf_thresh, top_k, gd_input_w,
                                                                 gd_input_h);
                } else if (post_type == "yolo_e2e") {
                    output.op = pipeline_utils::MakeYoloE2EPostOp(conf_thresh, top_k, gd_input_w, gd_input_h);
                } else if (post_type == "yolo_npu") {
                    std::vector<std::vector<std::vector<float>>> anchors;
                    std::vector<float> stride;
                    if (!ReadYoloNpuPostParams(p, anchors, stride)) {
                        return Status(COSMO_NN_ERR_PARAM,
                                      "Invalid yolo_npu anchors/stride config");
                    }
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
