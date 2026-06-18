#include "nn/pipeline/pipeline_utils.h"

#include "json/json.h"

namespace cosmo::nn {
namespace pipeline_utils {

    Resize* MakeResizeOp(const std::vector<int>& dsize, int gravity, const std::vector<int>& color) {
        auto* op    = new Resize("resize");
        op->dsize   = dsize;
        op->gravity = gravity;
        op->color   = color;
        return op;
    }

    Normalize* MakeNormalizeOp(const std::vector<float>& mean, float scale, bool is_bgr,
                               const std::vector<float>& std_dev) {
        auto* op   = new Normalize("normalize");
        op->mean   = mean;
        op->scale  = scale;
        op->is_bgr = is_bgr;
        if (!std_dev.empty())
            op->std = std_dev;
        return op;
    }

    AffineCrop* MakeAffineCropOp(float norm_ratio, int norm_mode, const std::vector<int>& output_hw,
                                 const std::vector<int>& center_index) {
        auto* op         = new AffineCrop("affine_crop");
        op->norm_ratio   = norm_ratio;
        op->norm_mode    = norm_mode;
        op->output_hw    = output_hw;
        op->center_index = center_index;
        return op;
    }

    CropResize* MakeCropResizeOp(const std::string& type, const std::vector<float>& h_top_crop,
                                 const std::vector<float>& h_bottom_crop,
                                 const std::vector<float>& w_left_crop,
                                 const std::vector<float>& w_right_crop, bool square, int square_mode,
                                 const std::vector<int>& dsize, int gravity, const std::vector<int>& color) {
        auto* op          = new CropResize("crop_resize");
        op->type          = type;
        op->h_top_crop    = h_top_crop;
        op->h_bottom_crop = h_bottom_crop;
        op->w_left_crop   = w_left_crop;
        op->w_right_crop  = w_right_crop;
        op->square        = square;
        op->square_mode   = square_mode;
        op->dsize         = dsize;
        op->gravity       = gravity;
        op->color         = color;
        return op;
    }

    Sequence* MakeSequenceOp(int size, float scale, const std::vector<int>& dsize, bool is_bgr) {
        auto* op   = new Sequence("sequence");
        op->size   = size;
        op->scale  = scale;
        op->dsize  = dsize;
        op->is_bgr = is_bgr;
        return op;
    }

    YoloPost* MakeYoloPostOp(float nms_threshold, float nms_detection_conf, int top_k, int input_width,
                             int input_height) {
        auto* op               = new YoloPost("yolo_postprocess");
        op->nms_threshold      = nms_threshold;
        op->nms_detection_conf = nms_detection_conf;
        op->top_k              = top_k;
        op->input_width        = input_width;
        op->input_height       = input_height;
        return op;
    }

    YoloNpuPost* MakeYoloNpuPostOp(float nms_threshold, float nms_detection_conf, int top_k,
                                   const std::vector<std::vector<std::vector<float>>>& anchors,
                                   const std::vector<float>& stride) {
        auto* op               = new YoloNpuPost("yolo_npu_postprocess");
        op->nms_threshold      = nms_threshold;
        op->nms_detection_conf = nms_detection_conf;
        op->top_k              = top_k;
        op->anchors            = anchors;
        op->stride             = stride;
        return op;
    }

    YoloPost* MakeYoloV8PostOp(float nms_threshold, float nms_detection_conf, int top_k, int input_width,
                               int input_height) {
        auto* op               = new YoloPost("yolov8_postprocess");
        op->nms_threshold      = nms_threshold;
        op->nms_detection_conf = nms_detection_conf;
        op->top_k              = top_k;
        op->input_width        = input_width;
        op->input_height       = input_height;
        return op;
    }

    YoloPost* MakeYoloE2EPostOp(float conf_threshold, int top_k, int input_width, int input_height) {
        auto* op               = new YoloPost("yolo_e2e_postprocess");
        op->nms_threshold      = 0;
        op->nms_detection_conf = conf_threshold;
        op->top_k              = top_k;
        op->input_width        = input_width;
        op->input_height       = input_height;
        return op;
    }

    DinoEncoder* MakeDinoEncoderOp(int dst_width, int dst_height, bool is_bgr, const std::vector<float>& mean,
                                   const std::vector<float>& std_dev) {
        auto* op       = new DinoEncoder("dino_encode");
        op->dst_width  = dst_width;
        op->dst_height = dst_height;
        op->is_bgr     = is_bgr;
        op->mean       = mean;
        op->std        = std_dev;
        return op;
    }

    DinoDecode* MakeDinoDecodeOp(float text_threshold, float box_threshold) {
        auto* op           = new DinoDecode("dino_decode");
        op->text_threshold = text_threshold;
        op->box_threshold  = box_threshold;
        return op;
    }

    SAMPromptEncode* MakeSAMPromptEncodeOp(const std::string& prompt_type, bool normalize, int encoder_size,
                                           int max_points) {
        auto* op         = new SAMPromptEncode("sam_prompt_encode");
        op->prompt_type  = prompt_type;
        op->normalize    = normalize;
        op->encoder_size = encoder_size;
        op->max_points   = max_points;
        return op;
    }

    SAMDecode* MakeSAMDecodeOp(float threshold, const std::vector<int>& output_size) {
        auto* op        = new SAMDecode("sam_decode");
        op->threshold   = threshold;
        op->output_size = output_size;
        return op;
    }

    void BuildInstructionsFromLabels(const std::vector<PipelineLabelInfo>& labels,
                                     const std::string& output_node_name, const DimsVector& output_shape,
                                     CombinedModelConfig& config) {
        if (labels.empty())
            return;

        Instruction instruction;
        instruction.output_node = output_node_name;
        instruction.shape       = output_shape;

        for (auto& label : labels) {
            InstructionOutputInfo info;
            info.label      = label.id;
            info.class_name = label.name;
            info.thresholds = label.threshold;
            instruction.infos.push_back(info);
        }

        config.instructions.push_back(instruction);
    }

    static std::vector<int> GetIntArray(const Json::Value& val) {
        std::vector<int> result;
        if (!val.isArray())
            return result;
        for (unsigned int i = 0; i < val.size(); i++)
            result.push_back(val[i].asInt());
        return result;
    }

    static std::vector<float> GetFloatArray(const Json::Value& val) {
        std::vector<float> result;
        if (!val.isArray())
            return result;
        for (unsigned int i = 0; i < val.size(); i++)
            result.push_back(val[i].asFloat());
        return result;
    }

    Status ParsePipelineConfig(const std::string& json_content, PipelineConfig& config) {
        Json::Reader reader;
        Json::Value root;
        if (!reader.parse(json_content, root))
            return Status(COSMO_NN_ERR_JSON_PARSE, "Failed to parse pipeline JSON");

        if (!root.isMember("model_type"))
            return Status(COSMO_NN_ERR_JSON_PARSE, "Missing 'model_type' field");

        config.model_type     = root["model_type"].asString();
        config.chip_type      = root.get("chip_type", "sophon").asString();
        config.algorithm_code = root.get("algorithm_code", "").asString();
        config.version        = root.get("version", "V1").asString();
        config.reduce         = root.get("reduce", "").asString();

        auto& models_json = root["models"];
        if (!models_json.isArray() || models_json.empty())
            return Status(COSMO_NN_ERR_JSON_PARSE, "Missing or empty 'models' array");

        Json::FastWriter writer;
        for (unsigned int i = 0; i < models_json.size(); i++) {
            auto& m = models_json[i];
            PipelineModelConfig model;
            model.name      = m.get("name", "").asString();
            model.file_name = m.get("file_name", "").asString();
            model.file_md5  = m.get("file_md5", "").asString();
            model.max_batch = m.get("max_batch", 1).asInt();

            if (m.isMember("inputs") && m["inputs"].isArray()) {
                for (unsigned int j = 0; j < m["inputs"].size(); j++) {
                    auto& in = m["inputs"][j];
                    PipelineModelConfig::InputDef input;
                    input.name      = in.get("name", "").asString();
                    input.shape     = GetIntArray(in["shape"]);
                    input.data_type = in.get("data_type", 0).asInt();
                    model.inputs.push_back(input);
                }
            }

            if (m.isMember("outputs") && m["outputs"].isArray()) {
                for (unsigned int j = 0; j < m["outputs"].size(); j++) {
                    auto& out = m["outputs"][j];
                    PipelineModelConfig::OutputDef output;
                    output.name      = out.get("name", "").asString();
                    output.shape     = GetIntArray(out["shape"]);
                    output.data_type = out.get("data_type", 0).asInt();
                    model.outputs.push_back(output);
                }
            }

            if (m.isMember("params"))
                model.params_json = writer.write(m["params"]);

            config.models.push_back(model);
        }

        if (root.isMember("labels") && root["labels"].isArray()) {
            for (unsigned int i = 0; i < root["labels"].size(); i++) {
                auto& l = root["labels"][i];
                PipelineLabelInfo label;
                label.id        = l.get("id", "").asString();
                label.name      = l.get("name", "").asString();
                label.threshold = GetFloatArray(l["threshold"]);
                config.labels.push_back(label);
            }
        }

        if (root.isMember("config"))
            config.extra_config_json = writer.write(root["config"]);

        return COSMO_NN_OK;
    }

}  // namespace pipeline_utils
}  // namespace cosmo::nn
