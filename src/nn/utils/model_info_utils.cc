#include "nn/utils/model_info_utils.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "nn/core/macros.h"

namespace cosmo::nn {

namespace {

const nlohmann::json* FindMember(const nlohmann::json& value, const char* key) {
    if (!value.is_object())
        return nullptr;
    auto iter = value.find(key);
    if (iter == value.end())
        return nullptr;
    return &(*iter);
}

const nlohmann::json& NullJson() {
    static const nlohmann::json value;
    return value;
}

const nlohmann::json& GetMemberOrNull(const nlohmann::json& value, const char* key) {
    const nlohmann::json* member = FindMember(value, key);
    return member ? *member : NullJson();
}

int ToInt(const nlohmann::json& value) {
    if (value.is_number_integer())
        return value.get<int>();
    return static_cast<int>(value.get<double>());
}

float ToFloat(const nlohmann::json& value) {
    return value.get<float>();
}

}  // namespace

void CheckNull(const nlohmann::json& value, std::string msg) noexcept(false) {
    if (value.is_null())
        throw std::runtime_error(msg);
}

void CheckObject(const nlohmann::json& value, std::string msg) noexcept(false) {
    if (!value.is_object())
        throw std::runtime_error(msg);
}

void CheckNullAndObject(const nlohmann::json& value, std::string msg) noexcept(false) {
    if (value.is_null())
        throw std::runtime_error(msg);
    if (!value.is_object())
        throw std::runtime_error(msg);
}

void CheckArray(const nlohmann::json& value, std::string msg) noexcept(false) {
    if (!value.is_array())
        throw std::runtime_error(msg);
}

void CheckNullAndArray(const nlohmann::json& value, std::string msg) noexcept(false) {
    if (value.is_null())
        throw std::runtime_error(msg);
    if (!value.is_array())
        throw std::runtime_error(msg);
}

template <typename T>
T Get(const nlohmann::json& value, const char* key) noexcept(false);

template <typename T>
T Get(const nlohmann::json& value, const char* key, T def) noexcept(false);

template <>
std::string Get<std::string>(const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        return "";
    if (!v.is_string())
        throw std::runtime_error(std::string(key) + " value must be String.");

    return v.get<std::string>();
}

template <>
std::string Get<std::string>(const nlohmann::json& value, const char* key,
                             std::string default_value) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        return default_value;

    if (!v.is_string())
        throw std::runtime_error(std::string(key) + " value must be String.");

    return v.get<std::string>();
}

template <>
int Get<int>(const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.is_number_integer())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return v.get<int>();
}

template <>
int Get<int>(const nlohmann::json& value, const char* key, int default_value) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        return default_value;

    if (!v.is_number())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return ToInt(v);
}

template <>
std::vector<int> Get<std::vector<int>>(const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    std::vector<int> result;
    if (v.is_null())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.is_array())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    for (const auto& element : v) {
        if (element.is_null())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.is_number_integer())
            throw std::runtime_error(std::string(key) + " element value must be Int.");

        result.push_back(element.get<int>());
    }
    return result;
}

template <>
std::vector<int> Get<std::vector<int>>(const nlohmann::json& value, const char* key,
                                       std::vector<int> /*def*/) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    std::vector<int> result;
    if (v.is_null())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.is_array())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    for (const auto& element : v) {
        if (element.is_null())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.is_number_integer())
            throw std::runtime_error(std::string(key) + " element value must be Int.");

        result.push_back(element.get<int>());
    }
    return result;
}

template <>
nlohmann::json Get<nlohmann::json>(const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    return v;
}

template <>
nlohmann::json Get<nlohmann::json>(const nlohmann::json& value, const char* key,
                                   nlohmann::json def) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        return def;
    return v;
}

template <>
float Get<float>(const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.is_number())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return ToFloat(v);
}

template <>
float Get<float>(const nlohmann::json& value, const char* key, float default_value) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        return default_value;

    if (!v.is_number())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return ToFloat(v);
}

template <>
std::vector<float> Get<std::vector<float>>(const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    std::vector<float> result;
    if (v.is_null())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.is_array())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    for (const auto& element : v) {
        if (element.is_null())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.is_number())
            throw std::runtime_error(std::string(key) + " element value must be Numeric.");

        result.push_back(ToFloat(element));
    }
    return result;
}

template <>
std::vector<float> Get<std::vector<float>>(const nlohmann::json& value, const char* key,
                                           std::vector<float> def) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        return def;
    std::vector<float> result;

    if (!v.is_array())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    for (const auto& element : v) {
        if (element.is_null())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.is_number())
            throw std::runtime_error(std::string(key) + " element value must be Numeric.");

        result.push_back(ToFloat(element));
    }
    return result;
}

template <>
std::vector<std::vector<std::vector<float>>> Get<std::vector<std::vector<std::vector<float>>>>(
    const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);

    if (v.is_null())
        throw std::runtime_error(std::string(key) + " first layer value must not be Null.");

    if (!v.is_array())
        throw std::runtime_error(std::string(key) + " first layer value must be Array.");

    std::vector<std::vector<std::vector<float>>> result;
    for (const auto& v1 : v) {
        if (v1.is_null())
            throw std::runtime_error(std::string(key) + " second layer value must not be Null.");

        if (!v1.is_array())
            throw std::runtime_error(std::string(key) + " second layer value must be Array.");

        std::vector<std::vector<float>> data_v1;
        for (const auto& v2 : v1) {
            if (v2.is_null())
                throw std::runtime_error(std::string(key) + " third layer value must not be Null.");

            if (!v2.is_array())
                throw std::runtime_error(std::string(key) + " third layer value must be Array.");

            std::vector<float> data_v2;
            for (const auto& element : v2) {
                if (element.is_null())
                    throw std::runtime_error(std::string(key) + " element value must not be Null.");

                if (!element.is_number())
                    throw std::runtime_error(std::string(key) + " element value must be Numeric.");

                data_v2.push_back(ToFloat(element));
            }
            data_v1.push_back(data_v2);
        }
        result.push_back(data_v1);
    }
    return result;
}

template <>
bool Get<bool>(const nlohmann::json& value, const char* key) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.is_boolean())
        throw std::runtime_error(std::string(key) + " value must be Bool.");

    return v.get<bool>();
}

template <>
bool Get<bool>(const nlohmann::json& value, const char* key, bool default_value) noexcept(false) {
    const nlohmann::json& v = GetMemberOrNull(value, key);
    if (v.is_null())
        return default_value;
    if (!v.is_boolean())
        throw std::runtime_error(std::string(key) + " value must be Bool.");

    return v.get<bool>();
}

Status ModelInfoUtils::LoadJson(const std::string& json_path, std::string& file_content) {
    std::ifstream file(json_path, std::ios::binary);
    if (!file.is_open()) {
        return Status(COSMO_NN_ERR_JSON_PARSE, "Load json file failed.");
    }
    file.seekg(0, file.end);
    std::streamsize size = file.tellg();
    if (size <= 0 || size > 10 * 1024 * 1024) {
        file.close();
        return Status(COSMO_NN_ERR_JSON_PARSE, "LoadJson: invalid file size (<=0 or >10MB)");
    }
    std::vector<char> content(static_cast<size_t>(size));
    file.seekg(0, file.beg);
    file.read(content.data(), size);
    file.close();
    file_content.assign(content.data(), static_cast<size_t>(size));
    return COSMO_NN_OK;
}

Status ParseModelsConfig(nlohmann::json& config_value, CombinedModelConfig& config) {
    try {
        CheckObject(config_value, "config value must be Object.");
        // face_info
        auto face_info_value =
            Get<nlohmann::json>(config_value, "feature_info", nlohmann::json());
        if (!face_info_value.is_null()) {
            CheckObject(face_info_value, "face info value must be Object.");

            config.face_info.testset_name =
                Get<std::string>(face_info_value, "testset_name", "");
            config.face_info.score_level =
                Get<std::vector<float>>(face_info_value, "score_level");
            config.face_info.cmp_score =
                Get<std::vector<float>>(face_info_value, "cmp_score");
            config.face_info.feature_dim = Get<int>(face_info_value, "feature_dim");
        }

        // instruction
        auto instructions_value =
            Get<nlohmann::json>(config_value, "instruction", nlohmann::json());
        if (instructions_value.is_null())
            return COSMO_NN_OK;

        CheckArray(instructions_value, "instruction must be Array.");
        auto instructions_size = instructions_value.size();
        for (unsigned int i = 0; i < instructions_size; i++) {
            Instruction instruction;
            auto instruction_value = instructions_value[i];
            CheckNullAndObject(instruction_value, "instruction element value must be Object.");

            instruction.output_node = Get<std::string>(instruction_value, "output_node");
            instruction.shape       = Get<DimsVector>(instruction_value, "shape");

            auto instruction_categories_value = Get<nlohmann::json>(
                instruction_value, "categories", nlohmann::json());
            if (!instruction_categories_value.is_null()) {
                CheckArray(instruction_categories_value, "categories must be Array.");
                auto instruction_categories_size = instruction_categories_value.size();
                for (unsigned int i = 0; i < instruction_categories_size; i++) {
                    nlohmann::json instruction_categories_element_value = instruction_categories_value[i];
                    CheckNullAndObject(instruction_categories_element_value,
                                       "instruction categories element must be Object.");
                    CategoryInfo category_info;
                    category_info.class_name = Get<std::string>(instruction_categories_element_value,
                                                                "class_name");
                    category_info.split =
                        Get<int>(instruction_categories_element_value, "split");
                    category_info.threshold = Get<std::vector<float>>(instruction_categories_element_value,
                                                                      "threshold");
                    instruction.categories.emplace_back(category_info);
                }
            }

            auto instruction_output_infos_value =
                Get<nlohmann::json>(instruction_value, "output_info");
            CheckArray(instruction_output_infos_value, "instruction output info must be Array");
            auto instruction_output_info_size = instruction_output_infos_value.size();
            instruction.infos.resize(instruction_output_info_size);
            for (unsigned int i = 0; i < instruction_output_info_size; i++) {
                auto instruction_output_info_value = instruction_output_infos_value[i];
                CheckNullAndObject(instruction_output_info_value, "output_info must be Object");

                instruction.infos.at(i).label =
                    Get<std::string>(instruction_output_info_value, "label");
                instruction.infos.at(i).class_name =
                    Get<std::string>(instruction_output_info_value, "class_name");
                instruction.infos.at(i).thresholds =
                    Get<std::vector<float>>(instruction_output_info_value, "threshold");
            }

            config.instructions.emplace_back(instruction);
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputOp(nlohmann::json& op_value, std::unique_ptr<Op>& op_out) {
    try {
        auto op_name = Get<std::string>(op_value, "op");
        // todo: add more ops
        if (op_name == "resize") {
            auto resize     = std::make_unique<Resize>();
            resize->dsize   = Get<std::vector<int>>(op_value, "dsize");
            resize->gravity = Get<int>(op_value, "gravity",
                                       Get<int>(op_value, "padding_gravity", 0));
            resize->color   = Get<std::vector<int>>(op_value, "color");
            resize->skip    = Get<bool>(op_value, "skip", false);
            op_out          = std::move(resize);
            return COSMO_NN_OK;
        } else if (op_name == "normalize") {
            auto normalize  = std::make_unique<Normalize>();
            normalize->mean = Get<std::vector<float>>(op_value, "mean");
            normalize->std =
                Get<std::vector<float>>(op_value, "std", std::vector<float>());
            normalize->scale  = Get<float>(op_value, "scale");
            normalize->is_bgr = Get<bool>(op_value, "is_bgr");
            normalize->skip   = Get<bool>(op_value, "skip", false);
            op_out            = std::move(normalize);
            return COSMO_NN_OK;
        } else if (op_name == "crop" || op_name == "expand") {
            auto rect_crop  = std::make_unique<RectCrop>();
            rect_crop->type = op_name;

            // Unified signed ratio [-1, 1]: negative or 0 means crop on that side, positive means expand
            // (computed by sign at runtime)
            float top    = Get<float>(op_value, "h_top_crop", 0.0f);
            float bottom = Get<float>(op_value, "h_bottom_crop", 0.0f);
            float left   = Get<float>(op_value, "w_left_crop", 0.0f);
            float right  = Get<float>(op_value, "w_right_crop", 0.0f);

            rect_crop->h_top_crop    = {top};
            rect_crop->h_bottom_crop = {bottom};
            rect_crop->w_left_crop   = {left};
            rect_crop->w_right_crop  = {right};
            rect_crop->bbox_hw_ratio_levels.clear();

            rect_crop->square      = Get<bool>(op_value, "square", false);
            rect_crop->square_mode = Get<int>(op_value, "square_mode", 0);
            rect_crop->skip        = Get<bool>(op_value, "skip", false);

            op_out = std::move(rect_crop);
            return COSMO_NN_OK;
        } else if (op_name == "affine_crop") {
            auto affine       = std::make_unique<AffineCrop>();
            affine->norm_mode = Get<int>(op_value, "NormMode");
            if (affine->norm_mode < 0 || affine->norm_mode > 2)
                return Status(COSMO_NN_ERR_JSON_PARSE, "affine NormMode tag must be in [0, 2].");

            affine->norm_ratio   = Get<float>(op_value, "norm_ratio");
            affine->output_hw    = Get<std::vector<int>>(op_value, "output_hw");
            affine->center_index = Get<std::vector<int>>(op_value, "center_index");
            affine->skip         = Get<bool>(op_value, "skip", false);
            op_out               = std::move(affine);
            return COSMO_NN_OK;
        } else if (op_name == "sequence") {
            auto sequence    = std::make_unique<Sequence>();
            sequence->size   = Get<int>(op_value, "size");
            sequence->scale  = Get<float>(op_value, "scale");
            sequence->dsize  = Get<std::vector<int>>(op_value, "dsize");
            sequence->is_bgr = Get<bool>(op_value, "is_bgr");
            sequence->skip   = Get<bool>(op_value, "skip", false);
            op_out           = std::move(sequence);
            return COSMO_NN_OK;
        } else if (op_name == "combine_image") {
            auto combine_image        = std::make_unique<CombineImage>();
            combine_image->count      = Get<int>(op_value, "count");
            combine_image->dst_height = Get<int>(op_value, "dst_height");
            combine_image->dst_width  = Get<int>(op_value, "dst_width");
            combine_image->skip       = Get<bool>(op_value, "skip", false);
            op_out                    = std::move(combine_image);
            return COSMO_NN_OK;
        } else if (op_name == "dino_encode") {
            auto dino_encode        = std::make_unique<DinoEncoder>();
            dino_encode->dst_height = Get<int>(op_value, "dst_height");
            dino_encode->dst_width  = Get<int>(op_value, "dst_width");
            dino_encode->is_bgr     = Get<bool>(op_value, "is_bgr");
            dino_encode->mean       = Get<std::vector<float>>(op_value, "mean");
            dino_encode->std        = Get<std::vector<float>>(op_value, "std");
            dino_encode->skip       = Get<bool>(op_value, "skip", false);
            op_out                  = std::move(dino_encode);
            return COSMO_NN_OK;
        } else if (op_name == "sam_prompt_encode") {
            auto sam_prompt_encode = std::make_unique<SAMPromptEncode>();
            sam_prompt_encode->prompt_type =
                Get<std::string>(op_value, "prompt_type", "point");
            sam_prompt_encode->normalize    = Get<bool>(op_value, "normalize", true);
            sam_prompt_encode->encoder_size = Get<int>(op_value, "encoder_size", 1024);
            sam_prompt_encode->max_points   = Get<int>(op_value, "max_points", 6);
            sam_prompt_encode->skip         = Get<bool>(op_value, "skip", false);
            op_out                          = std::move(sam_prompt_encode);
            return COSMO_NN_OK;
        } else {
            return Status(COSMO_NN_ERR_JSON_PARSE, "Unsupport input op.");
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelOutputOp(nlohmann::json& op_value, std::unique_ptr<Op>& op_out) {
    try {
        CheckObject(op_value, "output post_process node must be Object.");
        auto op_name = Get<std::string>(op_value, "op");
        // todo: add more post process op
        if (op_name == "yolo_postprocess") {
            auto yolo_post                = std::make_unique<YoloPost>();
            yolo_post->nms_threshold      = Get<float>(op_value, "nms_threshold");
            yolo_post->nms_detection_conf = Get<float>(op_value, "nms_detection_conf");
            yolo_post->top_k              = Get<int>(op_value, "top_k");
            op_out                        = std::move(yolo_post);
            return COSMO_NN_OK;
        } else if (op_name == "yolo_npu_postprocess") {
            auto yolo_npu_post           = std::make_unique<YoloNpuPost>();
            yolo_npu_post->nms_threshold = Get<float>(op_value, "nms_threshold");
            yolo_npu_post->nms_detection_conf =
                Get<float>(op_value, "nms_detection_conf");
            yolo_npu_post->top_k = Get<int>(op_value, "top_k");
            yolo_npu_post->anchors =
                Get<std::vector<std::vector<std::vector<float>>>>(op_value, "anchors");
            yolo_npu_post->stride = Get<std::vector<float>>(op_value, "stride");
            std::string des       = yolo_npu_post->Description();
            op_out                = std::move(yolo_npu_post);
            return COSMO_NN_OK;
        } else if (op_name == "yolov8_postprocess") {
            // YOLOv8 uses the same output format as YOLOv5 when anchor decoding is in the model
            auto yolov8_post                = std::make_unique<YoloPost>("yolov8_postprocess");
            yolov8_post->nms_threshold      = Get<float>(op_value, "nms_threshold");
            yolov8_post->nms_detection_conf = Get<float>(op_value, "nms_detection_conf");
            yolov8_post->top_k              = Get<int>(op_value, "top_k");
            op_out                          = std::move(yolov8_post);
            return COSMO_NN_OK;
        } else if (op_name == "sum") {
            op_out = std::make_unique<Sum>();
            return COSMO_NN_OK;
        } else if (op_name == "arg_max") {
            auto arg_max  = std::make_unique<ArgMax>();
            arg_max->axis = Get<int>(op_value, "axis");
            op_out        = std::move(arg_max);
            return COSMO_NN_OK;
        } else if (op_name == "split") {
            auto split_op   = std::make_unique<Split>();
            split_op->axis  = Get<int>(op_value, "axis");
            split_op->split = Get<std::vector<int>>(op_value, "split");
            op_out          = std::move(split_op);
            return COSMO_NN_OK;
        } else if (op_name == "split_arg_max") {
            auto split_arg_max   = std::make_unique<SplitArgMax>();
            split_arg_max->split = Get<DimsVector>(op_value, "split");
            op_out               = std::move(split_arg_max);
            return COSMO_NN_OK;
        } else if (op_name == "dino_decode") {
            auto dino_decode            = std::make_unique<DinoDecode>();
            dino_decode->text_threshold = Get<float>(op_value, "text_threshold");
            dino_decode->box_threshold  = Get<float>(op_value, "box_threshold");
            op_out                      = std::move(dino_decode);
            return COSMO_NN_OK;
        } else if (op_name == "sam_decode") {
            auto sam_decode       = std::make_unique<SAMDecode>();
            sam_decode->threshold = Get<float>(op_value, "threshold", 0.0f);
            sam_decode->output_size =
                Get<std::vector<int>>(op_value, "output_size", std::vector<int>());
            sam_decode->skip = Get<bool>(op_value, "skip", false);
            op_out           = std::move(sam_decode);
            return COSMO_NN_OK;
        } else {
            return Status(COSMO_NN_ERR_JSON_PARSE, "Unsupport output op");
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputOps(nlohmann::json& ops_value, std::vector<std::unique_ptr<Op>>& ops) {
    try {
        auto input_node_ops_size = ops_value.size();
        if (input_node_ops_size < 1)
            return Status(COSMO_NN_ERR_JSON_PARSE, "ops can not be empty.");

        ops.resize(input_node_ops_size);
        for (unsigned int i = 0; i < input_node_ops_size; i++) {
            nlohmann::json op_value = ops_value[i];
            CheckNullAndObject(op_value, "preprocess element must not be null and must be Object.");
            RETURN_ON_FAIL(ParseModelInputOp(op_value, ops.at(i)));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputNode(nlohmann::json& input_node_info_value, InputNodeInfo& input_node) {
    try {
        input_node.name  = Get<std::string>(input_node_info_value, "input_node");
        input_node.shape = Get<DimsVector>(input_node_info_value, "shape");

        input_node.data_type      = Get<int>(input_node_info_value, "data_type");
        auto input_node_ops_value = Get<nlohmann::json>(input_node_info_value, "preprocess",
                                                     nlohmann::json());
        if (!input_node_ops_value.is_null()) {
            CheckArray(input_node_ops_value, "input node ops must be Array.");
            RETURN_ON_FAIL(ParseModelInputOps(input_node_ops_value, input_node.ops));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputNodes(nlohmann::json& input_node_infos_value,
                            std::vector<InputNodeInfo>& input_node_infos) {
    try {
        auto input_node_infos_size = input_node_infos_value.size();
        if (input_node_infos_size < 1)
            return Status(COSMO_NN_ERR_JSON_PARSE, "inputs must have at least one node.");

        input_node_infos.resize(input_node_infos_size);
        for (unsigned int i = 0; i < input_node_infos_size; i++) {
            auto input_node_info_value = input_node_infos_value[i];
            CheckNullAndObject(input_node_info_value, "input_node_infos element must be Object.");
            RETURN_ON_FAIL(ParseModelInputNode(input_node_info_value, input_node_infos.at(i)));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ParseModelOutputNode(nlohmann::json& output_node_value, OutputNodeInfo& output_node) {
    try {
        output_node.name      = Get<std::string>(output_node_value, "output_node");
        output_node.shape     = Get<DimsVector>(output_node_value, "shape");
        output_node.data_type = Get<int>(output_node_value, "data_type");
        auto output_node_post_process_value = Get<nlohmann::json>(
            output_node_value, "post_process", nlohmann::json());
        if (!output_node_post_process_value.is_null())
            RETURN_ON_FAIL(ParseModelOutputOp(output_node_post_process_value, output_node.op));
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelOutputNodes(nlohmann::json& output_node_infos_value,
                             std::vector<OutputNodeInfo>& output_node_infos) {
    try {
        auto output_node_size = output_node_infos_value.size();
        if (output_node_size < 1)
            return Status(COSMO_NN_ERR_JSON_PARSE, "output_node_infos must have at least one node.");

        output_node_infos.resize(output_node_size);
        for (unsigned int i = 0; i < output_node_size; i++) {
            nlohmann::json output_node_value = output_node_infos_value[i];
            CheckNullAndObject(output_node_value, "output_node_infos element must be Object.");
            RETURN_ON_FAIL(ParseModelOutputNode(output_node_value, output_node_infos.at(i)));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInfoInner(nlohmann::json& info_value, ModelInfo& model_info) {
    try {
        model_info.description      = Get<std::string>(info_value, "description", "");
        model_info.name             = Get<std::string>(info_value, "name");
        model_info.filename         = Get<std::string>(info_value, "file_name");
        model_info.file_md5         = Get<std::string>(info_value, "file_MD5", "");
        model_info.max_batch        = Get<int>(info_value, "max_batch");
        auto input_node_infos_value = Get<nlohmann::json>(info_value, "inputs");
        CheckArray(input_node_infos_value, "inputs must be Array.");
        RETURN_ON_FAIL(ParseModelInputNodes(input_node_infos_value, model_info.input_node_infos));

        auto output_node_infos_value = Get<nlohmann::json>(info_value, "outputs");
        CheckArray(output_node_infos_value, "outputs must be Array.");
        RETURN_ON_FAIL(ParseModelOutputNodes(output_node_infos_value, model_info.output_node_infos));
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ParseModelsInfo(nlohmann::json& model_value, std::vector<ModelInfo>& models) {
    try {
        CheckArray(model_value, "model must be Array.");
        auto model_size = model_value.size();
        if (model_size < 1)
            return Status(COSMO_NN_ERR_JSON_PARSE, "Json must have at least one model info.");

        models.resize(model_size);
        for (unsigned int i = 0; i < model_size; i++) {
            auto model_info_value = model_value[i];
            CheckNullAndObject(model_info_value, "model info must be Object.");
            RETURN_ON_FAIL(ParseModelInfoInner(model_info_value, models.at(i)));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ParseModelConvert(nlohmann::json& convert_value, Convert& convert) {
    try {
        CheckObject(convert_value, "config value must be Object.");
        convert.type      = Get<std::string>(convert_value, "type");
        convert.max_batch = Get<int>(convert_value, "max_batch");
        convert.precision = Get<std::string>(convert_value, "precision");
        auto models_value =
            Get<nlohmann::json>(convert_value, "model", nlohmann::json());
        if (models_value.is_null())
            return COSMO_NN_OK;

        CheckArray(models_value, "model must be Array.");
        convert.models.resize(models_value.size());
        for (unsigned int i = 0; i < models_value.size(); i++) {
            auto model_value = models_value[i];
            CheckNullAndObject(model_value, "inner must be Object.");
            convert.models.at(i).mean  = Get<std::vector<float>>(model_value, "mean", {});
            convert.models.at(i).scale = Get<float>(model_value, "scale", 0.f);
            convert.models.at(i).is_bgr = Get<bool>(model_value, "is_bgr", false);
            convert.models.at(i).is_opconvert =
                Get<bool>(model_value, "is_opconvert", true);
            convert.models.at(i).is_optimize =
                Get<bool>(model_value, "is_optimize", true);
            convert.models.at(i).is_normalize =
                Get<bool>(model_value, "is_normalize", false);
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ModelInfoUtils::ParseModelInfo(const std::string& info_content_, CombinedModelInfo& info) {
    nlohmann::json root = nlohmann::json::parse(info_content_, nullptr, false);
    if (root.is_discarded()) {
        return Status(COSMO_NN_ERR_JSON_PARSE, "Failed to parse model info JSON");
    }

    try {
        CheckNullAndObject(root, "root must be Object.");
        info.reduce        = Get<std::string>(root, "reduce", "");
        info.type          = Get<std::string>(root, "type");
        info.algorithmcode = Get<std::string>(root, "algorithmcode");
        auto model_value   = Get<nlohmann::json>(root, "model");
        RETURN_ON_FAIL(ParseModelsInfo(model_value, info.models));

        auto config_value =
            Get<nlohmann::json>(root, "config", nlohmann::json());
        if (!config_value.is_null())
            RETURN_ON_FAIL(ParseModelsConfig(config_value, info.config));

        auto convert_value =
            Get<nlohmann::json>(root, "cwnn_convert", nlohmann::json());
        if (!convert_value.is_null())
            RETURN_ON_FAIL(ParseModelConvert(convert_value, info.convert));

    } catch (const std::runtime_error& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    } catch (const std::invalid_argument& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ModelInfoUtils::GetInputShapesMap(const ModelInfo& model_info_, ShapesMap& shapes) {
    const auto& inputs_node_info = model_info_.input_node_infos;
    if (inputs_node_info.empty())
        return Status(COSMO_NN_ERR_JSON_INVALID_INPUT, "Json must hava input node info");

    int max_batch = model_info_.max_batch;
    if (max_batch < 1)
        return Status(COSMO_NN_ERR_JSON_INVALID_BATCH, "Invalid max batch");

    int input_num = inputs_node_info.size();
    shapes.clear();

    for (int i = 0; i < input_num; i++) {
        std::string name = inputs_node_info.at(i).name;
        DimsVector shape = inputs_node_info.at(i).shape;

        // Only set max_batch if shape is not empty
        // Some inputs (like decoder inputs without preprocessing) may have empty shapes
        if (!shape.empty()) {
            shape.at(0) = max_batch;
        }

        shapes[name] = shape;
    }

    return COSMO_NN_OK;
}

void ModelInfoUtils::GetSelectedThreshold(const CombinedModelInfo& info,
                                          std::vector<std::vector<float>>& thresholds, int index) {
    if (info.config.instructions.empty())
        return;

    if (info.config.instructions.at(0).infos.empty())
        return;

    if (index < 0 || index >= static_cast<int>(info.config.instructions.at(0).infos.at(0).thresholds.size()))
        return;

    thresholds.clear();
    for (auto item : info.config.instructions) {
        std::vector<float> node_tresholds;
        std::transform(item.infos.begin(), item.infos.end(), std::back_inserter(node_tresholds),
                       [index](const InstructionOutputInfo& info) { return info.thresholds.at(index); });
        thresholds.emplace_back(node_tresholds);
    }
}

void ModelInfoUtils::GetSelectedClassName(const CombinedModelInfo& info,
                                          std::vector<std::vector<std::string>>& names) {
    names.clear();
    for (auto item : info.config.instructions) {
        std::vector<std::string> node_labels;
        std::transform(item.infos.begin(), item.infos.end(), std::back_inserter(node_labels),
                       [](const InstructionOutputInfo& info) { return info.class_name; });

        names.emplace_back(node_labels);
    }
}

void ModelInfoUtils::GetSelectedIndex(const CombinedModelInfo& info, std::vector<int>& indices) {
    indices.clear();
    for (auto item : info.config.instructions) {
        std::transform(item.infos.begin(), item.infos.end(), std::back_inserter(indices),
                       [](const InstructionOutputInfo& info) { return std::atoi(info.label.c_str()); });
    }
}

}  // namespace cosmo::nn
