#include "nn/pipeline/pipeline_utils.h"

#include <utility>

#include <nlohmann/json.hpp>

#include "util/Log.h"

namespace cosmo::nn {
namespace pipeline_utils {
namespace {

    const nlohmann::json* FindObjectMember(const nlohmann::json& json, const char* key) {
        if (!json.is_object())
            return nullptr;
        auto iter = json.find(key);
        return iter == json.end() ? nullptr : &(*iter);
    }

    void WarnInvalidField(const char* key, const char* expected, const nlohmann::json& actual) {
        LOG_WARN("Invalid JSON field '{}' type: expected {}, actual {}; using default", key, expected,
                 actual.type_name());
    }

}  // namespace

    nlohmann::json ParseJsonObject(const std::string& raw) {
        if (raw.empty())
            return nlohmann::json::object();
        auto parsed = nlohmann::json::parse(raw, nullptr, false);
        return parsed.is_discarded() || !parsed.is_object() ? nlohmann::json::object() : parsed;
    }

    std::string ReadString(const nlohmann::json& json, const char* key, std::string defaults) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_string()) {
            WarnInvalidField(key, "string", *value);
            return defaults;
        }
        try {
            return value->get<std::string>();
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
            return defaults;
        }
    }

    int ReadInt(const nlohmann::json& json, const char* key, int defaults) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_number_integer()) {
            WarnInvalidField(key, "integer", *value);
            return defaults;
        }
        try {
            return value->get<int>();
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
            return defaults;
        }
    }

    size_t ReadSize(const nlohmann::json& json, const char* key, size_t defaults) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_number_integer()) {
            WarnInvalidField(key, "non-negative integer", *value);
            return defaults;
        }
        try {
            if (value->is_number_unsigned())
                return value->get<size_t>();
            const auto signed_value = value->get<long long>();
            if (signed_value < 0) {
                WarnInvalidField(key, "non-negative integer", *value);
                return defaults;
            }
            return static_cast<size_t>(signed_value);
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
            return defaults;
        }
    }

    float ReadFloat(const nlohmann::json& json, const char* key, float defaults) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_number()) {
            WarnInvalidField(key, "number", *value);
            return defaults;
        }
        try {
            return value->get<float>();
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
            return defaults;
        }
    }

    bool ReadBool(const nlohmann::json& json, const char* key, bool defaults) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_boolean()) {
            WarnInvalidField(key, "boolean", *value);
            return defaults;
        }
        try {
            return value->get<bool>();
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
            return defaults;
        }
    }

    std::vector<int> ReadIntArray(const nlohmann::json& json, const char* key,
                                  std::vector<int> defaults, size_t min_size) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_array()) {
            WarnInvalidField(key, "integer array", *value);
            return defaults;
        }

        std::vector<int> values;
        values.reserve(value->size());
        try {
            for (const auto& item : *value) {
                if (!item.is_number_integer()) {
                    WarnInvalidField(key, "integer array", item);
                    return defaults;
                }
                values.push_back(item.get<int>());
            }
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
            return defaults;
        }
        if (values.size() < min_size) {
            LOG_WARN("Invalid JSON field '{}' length: expected at least {}, actual {}; using default", key,
                     min_size, values.size());
            return defaults;
        }
        return values;
    }

    std::vector<float> ReadFloatArray(const nlohmann::json& json, const char* key,
                                      std::vector<float> defaults, size_t min_size) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_array()) {
            WarnInvalidField(key, "number array", *value);
            return defaults;
        }

        std::vector<float> values;
        values.reserve(value->size());
        try {
            for (const auto& item : *value) {
                if (!item.is_number()) {
                    WarnInvalidField(key, "number array", item);
                    return defaults;
                }
                values.push_back(item.get<float>());
            }
        } catch (const nlohmann::json::exception& e) {
            LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
            return defaults;
        }
        if (values.size() < min_size) {
            LOG_WARN("Invalid JSON field '{}' length: expected at least {}, actual {}; using default", key,
                     min_size, values.size());
            return defaults;
        }
        return values;
    }

    std::vector<std::vector<std::vector<float>>> ReadFloat3DArray(
        const nlohmann::json& json, const char* key,
        std::vector<std::vector<std::vector<float>>> defaults, size_t min_outer_size,
        size_t min_middle_size, size_t min_inner_size) {
        const nlohmann::json* value = FindObjectMember(json, key);
        if (!value || value->is_null())
            return defaults;
        if (!value->is_array()) {
            WarnInvalidField(key, "3D number array", *value);
            return defaults;
        }

        std::vector<std::vector<std::vector<float>>> values;
        values.reserve(value->size());
        for (const auto& outer_json : *value) {
            if (!outer_json.is_array()) {
                WarnInvalidField(key, "3D number array", outer_json);
                return defaults;
            }

            std::vector<std::vector<float>> outer;
            outer.reserve(outer_json.size());
            for (const auto& middle_json : outer_json) {
                if (!middle_json.is_array()) {
                    WarnInvalidField(key, "3D number array", middle_json);
                    return defaults;
                }

                std::vector<float> middle;
                middle.reserve(middle_json.size());
                try {
                    for (const auto& item : middle_json) {
                        if (!item.is_number()) {
                            WarnInvalidField(key, "3D number array", item);
                            return defaults;
                        }
                        middle.push_back(item.get<float>());
                    }
                } catch (const nlohmann::json::exception& e) {
                    LOG_WARN("Invalid JSON field '{}' conversion: {}; using default", key, e.what());
                    return defaults;
                }
                if (middle.size() < min_inner_size) {
                    LOG_WARN("Invalid JSON field '{}' inner length: expected at least {}, actual {}; using default",
                             key, min_inner_size, middle.size());
                    return defaults;
                }
                outer.push_back(std::move(middle));
            }
            if (outer.size() < min_middle_size) {
                LOG_WARN("Invalid JSON field '{}' middle length: expected at least {}, actual {}; using default",
                         key, min_middle_size, outer.size());
                return defaults;
            }
            values.push_back(std::move(outer));
        }
        if (values.size() < min_outer_size) {
            LOG_WARN("Invalid JSON field '{}' outer length: expected at least {}, actual {}; using default", key,
                     min_outer_size, values.size());
            return defaults;
        }
        return values;
    }

    std::unique_ptr<Resize> MakeResizeOp(const std::vector<int>& dsize, int gravity,
                                         const std::vector<int>& color) {
        auto op     = std::make_unique<Resize>("resize");
        op->dsize   = dsize;
        op->gravity = gravity;
        op->color   = color;
        return op;
    }

    std::unique_ptr<Normalize> MakeNormalizeOp(const std::vector<float>& mean, float scale, bool is_bgr,
                                               const std::vector<float>& std_dev) {
        auto op    = std::make_unique<Normalize>("normalize");
        op->mean   = mean;
        op->scale  = scale;
        op->is_bgr = is_bgr;
        if (!std_dev.empty())
            op->std = std_dev;
        return op;
    }

    std::unique_ptr<AffineCrop> MakeAffineCropOp(float norm_ratio, int norm_mode,
                                                 const std::vector<int>& output_hw,
                                                 const std::vector<int>& center_index) {
        auto op          = std::make_unique<AffineCrop>("affine_crop");
        op->norm_ratio   = norm_ratio;
        op->norm_mode    = norm_mode;
        op->output_hw    = output_hw;
        op->center_index = center_index;
        return op;
    }

    std::unique_ptr<CropResize> MakeCropResizeOp(const std::string& type,
                                                 const std::vector<float>& h_top_crop,
                                                 const std::vector<float>& h_bottom_crop,
                                                 const std::vector<float>& w_left_crop,
                                                 const std::vector<float>& w_right_crop, bool square,
                                                 int square_mode, const std::vector<int>& dsize, int gravity,
                                                 const std::vector<int>& color) {
        auto op           = std::make_unique<CropResize>("crop_resize");
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

    std::unique_ptr<Sequence> MakeSequenceOp(int size, float scale, const std::vector<int>& dsize,
                                             bool is_bgr) {
        auto op    = std::make_unique<Sequence>("sequence");
        op->size   = size;
        op->scale  = scale;
        op->dsize  = dsize;
        op->is_bgr = is_bgr;
        return op;
    }

    std::unique_ptr<YoloPost> MakeYoloPostOp(float nms_threshold, float nms_detection_conf, int top_k,
                                             int input_width, int input_height) {
        auto op                = std::make_unique<YoloPost>("yolo_postprocess");
        op->nms_threshold      = nms_threshold;
        op->nms_detection_conf = nms_detection_conf;
        op->top_k              = top_k;
        op->input_width        = input_width;
        op->input_height       = input_height;
        return op;
    }

    std::unique_ptr<YoloNpuPost> MakeYoloNpuPostOp(
        float nms_threshold, float nms_detection_conf, int top_k,
        const std::vector<std::vector<std::vector<float>>>& anchors, const std::vector<float>& stride) {
        auto op                = std::make_unique<YoloNpuPost>("yolo_npu_postprocess");
        op->nms_threshold      = nms_threshold;
        op->nms_detection_conf = nms_detection_conf;
        op->top_k              = top_k;
        op->anchors            = anchors;
        op->stride             = stride;
        return op;
    }

    std::unique_ptr<YoloPost> MakeYoloV8PostOp(float nms_threshold, float nms_detection_conf, int top_k,
                                               int input_width, int input_height) {
        auto op                = std::make_unique<YoloPost>("yolov8_postprocess");
        op->nms_threshold      = nms_threshold;
        op->nms_detection_conf = nms_detection_conf;
        op->top_k              = top_k;
        op->input_width        = input_width;
        op->input_height       = input_height;
        return op;
    }

    std::unique_ptr<YoloPost> MakeYoloE2EPostOp(float conf_threshold, int top_k, int input_width,
                                                int input_height) {
        auto op                = std::make_unique<YoloPost>("yolo_e2e_postprocess");
        op->nms_threshold      = 0;
        op->nms_detection_conf = conf_threshold;
        op->top_k              = top_k;
        op->input_width        = input_width;
        op->input_height       = input_height;
        return op;
    }

    std::unique_ptr<DinoEncoder> MakeDinoEncoderOp(int dst_width, int dst_height, bool is_bgr,
                                                   const std::vector<float>& mean,
                                                   const std::vector<float>& std_dev) {
        auto op        = std::make_unique<DinoEncoder>("dino_encode");
        op->dst_width  = dst_width;
        op->dst_height = dst_height;
        op->is_bgr     = is_bgr;
        op->mean       = mean;
        op->std        = std_dev;
        return op;
    }

    std::unique_ptr<DinoDecode> MakeDinoDecodeOp(float text_threshold, float box_threshold) {
        auto op            = std::make_unique<DinoDecode>("dino_decode");
        op->text_threshold = text_threshold;
        op->box_threshold  = box_threshold;
        return op;
    }

    std::unique_ptr<SAMPromptEncode> MakeSAMPromptEncodeOp(const std::string& prompt_type, bool normalize,
                                                           int encoder_size, int max_points) {
        auto op          = std::make_unique<SAMPromptEncode>("sam_prompt_encode");
        op->prompt_type  = prompt_type;
        op->normalize    = normalize;
        op->encoder_size = encoder_size;
        op->max_points   = max_points;
        return op;
    }

    std::unique_ptr<SAMDecode> MakeSAMDecodeOp(float threshold, const std::vector<int>& output_size) {
        auto op         = std::make_unique<SAMDecode>("sam_decode");
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

    static std::vector<int> GetIntArray(const nlohmann::json& val) {
        std::vector<int> result;
        if (!val.is_array())
            return result;
        for (const auto& item : val) {
            if (!item.is_number_integer())
                return {};
            result.push_back(item.get<int>());
        }
        return result;
    }

    static std::vector<float> GetFloatArray(const nlohmann::json& val) {
        std::vector<float> result;
        if (!val.is_array())
            return result;
        for (const auto& item : val) {
            if (!item.is_number())
                return {};
            result.push_back(item.get<float>());
        }
        return result;
    }

    Status ParsePipelineConfig(const std::string& json_content, PipelineConfig& config) {
        try {
            auto root = nlohmann::json::parse(json_content, nullptr, false);
            if (root.is_discarded())
                return Status(COSMO_NN_ERR_JSON_PARSE, "Failed to parse pipeline JSON");

            if (!root.contains("model_type"))
                return Status(COSMO_NN_ERR_JSON_PARSE, "Missing 'model_type' field");

            config.model_type     = ReadString(root, "model_type", std::string());
            config.chip_type      = ReadString(root, "chip_type", std::string("sophon"));
            config.algorithm_code = ReadString(root, "algorithm_code", std::string());
            config.version        = ReadString(root, "version", std::string("V1"));
            config.reduce         = ReadString(root, "reduce", std::string());

            const nlohmann::json* models_json = FindObjectMember(root, "models");
            if (!models_json || !models_json->is_array() || models_json->empty())
                return Status(COSMO_NN_ERR_JSON_PARSE, "Missing or empty 'models' array");

            for (const auto& m : *models_json) {
                PipelineModelConfig model;
                model.name      = ReadString(m, "name", std::string());
                model.file_name = ReadString(m, "file_name", std::string());
                model.file_md5  = ReadString(m, "file_md5", std::string());
                model.max_batch = ReadInt(m, "max_batch", 1);

                const nlohmann::json* inputs = FindObjectMember(m, "inputs");
                if (inputs && inputs->is_array()) {
                    for (const auto& in : *inputs) {
                        PipelineModelConfig::InputDef input;
                        input.name = ReadString(in, "name", std::string());
                        const nlohmann::json* shape = FindObjectMember(in, "shape");
                        input.shape                 = shape ? GetIntArray(*shape) : std::vector<int>();
                        input.data_type             = ReadInt(in, "data_type", 0);
                        model.inputs.push_back(input);
                    }
                }

                const nlohmann::json* outputs = FindObjectMember(m, "outputs");
                if (outputs && outputs->is_array()) {
                    for (const auto& out : *outputs) {
                        PipelineModelConfig::OutputDef output;
                        output.name = ReadString(out, "name", std::string());
                        const nlohmann::json* shape = FindObjectMember(out, "shape");
                        output.shape                = shape ? GetIntArray(*shape) : std::vector<int>();
                        output.data_type            = ReadInt(out, "data_type", 0);
                        model.outputs.push_back(output);
                    }
                }

                const nlohmann::json* params = FindObjectMember(m, "params");
                if (params)
                    model.params_json = params->dump();

                config.models.push_back(model);
            }

            const nlohmann::json* labels = FindObjectMember(root, "labels");
            if (labels && labels->is_array()) {
                for (const auto& l : *labels) {
                    PipelineLabelInfo label;
                    label.id   = ReadString(l, "id", std::string());
                    label.name = ReadString(l, "name", std::string());
                    const nlohmann::json* threshold = FindObjectMember(l, "threshold");
                    label.threshold = threshold ? GetFloatArray(*threshold) : std::vector<float>();
                    config.labels.push_back(label);
                }
            }

            const nlohmann::json* extra_config = FindObjectMember(root, "config");
            if (extra_config)
                config.extra_config_json = extra_config->dump();

            return COSMO_NN_OK;
        } catch (const nlohmann::json::exception& e) {
            return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
        }
    }

}  // namespace pipeline_utils
}  // namespace cosmo::nn
