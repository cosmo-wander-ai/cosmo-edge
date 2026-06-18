#include "nn/utils/model_info_utils.h"

#include <algorithm>
#include <fstream>

#include "json/json.h"
#include "nn/core/macros.h"

namespace cosmo::nn {

void CheckNull(Json::Value& value, std::string msg) noexcept(false) {
    if (value.isNull())
        throw std::runtime_error(msg);
}

void CheckObject(Json::Value& value, std::string msg) noexcept(false) {
    if (!value.isObject())
        throw std::runtime_error(msg);
}

void CheckNullAndObject(Json::Value& value, std::string msg) noexcept(false) {
    if (value.isNull())
        throw std::runtime_error(msg);
    if (!value.isObject())
        throw std::runtime_error(msg);
}

void CheckArray(Json::Value& value, std::string msg) noexcept(false) {
    if (!value.isArray())
        throw std::runtime_error(msg);
}

void CheckNullAndArray(Json::Value& value, std::string msg) noexcept(false) {
    if (value.isNull())
        throw std::runtime_error(msg);
    if (!value.isArray())
        throw std::runtime_error(msg);
}

template <typename T>
T Get(Json::Value& value, Json::StaticString key) noexcept(false);

template <typename T>
T Get(Json::Value& value, Json::StaticString key, T def) noexcept(false);

template <>
std::string Get<std::string>(Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        return "";
    if (!v.isString())
        throw std::runtime_error(std::string(key) + " value must be String.");

    return v.asString();
}

template <>
std::string Get<std::string>(Json::Value& value, Json::StaticString key,
                             std::string default_value) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        return default_value;

    if (!v.isString())
        throw std::runtime_error(std::string(key) + " value must be String.");

    return v.asString();
}

template <>
int Get<int>(Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.isInt())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return v.asInt();
}

template <>
int Get<int>(Json::Value& value, Json::StaticString key, int default_value) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        return default_value;

    if (!v.isNumeric())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return v.asInt();
}

template <>
std::vector<int> Get<std::vector<int>>(Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];
    std::vector<int> result;
    if (v.isNull())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.isArray())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    auto size = v.size();
    for (unsigned int i = 0; i < size; i++) {
        auto element = v[i];
        if (element.isNull())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.isInt())
            throw std::runtime_error(std::string(key) + " element value must be Int.");

        result.push_back(v[i].asInt());
    }
    return result;
}

template <>
std::vector<int> Get<std::vector<int>>(Json::Value& value, Json::StaticString key,
                                       std::vector<int> def) noexcept(false) {
    Json::Value v = value[key];
    std::vector<int> result;
    auto size = v.size();
    if (v.isNull())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.isArray())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    for (unsigned int i = 0; i < size; i++) {
        auto element = v[i];
        if (element.isNull())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.isInt())
            throw std::runtime_error(std::string(key) + " element value must be Int.");

        result.push_back(v[i].asInt());
    }
    return result;
}

template <>
Json::Value Get<Json::Value>(Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    return v;
}

template <>
Json::Value Get<Json::Value>(Json::Value& value, Json::StaticString key, Json::Value def) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        return def;
    return v;
}

template <>
float Get<float>(Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.isNumeric())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return v.asFloat();
}

template <>
float Get<float>(Json::Value& value, Json::StaticString key, float default_value) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        return default_value;

    if (!v.isNumeric())
        throw std::runtime_error(std::string(key) + " value must be Numeric.");

    return v.asFloat();
}

template <>
std::vector<float> Get<std::vector<float>>(Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];
    auto size     = v.size();
    std::vector<float> result;
    if (v.isNull())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.isArray())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    for (unsigned int i = 0; i < size; i++) {
        auto element = v[i];
        if (element.isNull())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.isDouble())
            throw std::runtime_error(std::string(key) + " element value must be Numeric.");

        result.push_back(element.asFloat());
    }
    return result;
}

template <>
std::vector<float> Get<std::vector<float>>(Json::Value& value, Json::StaticString key,
                                           std::vector<float> def) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        return def;
    auto size = v.size();
    std::vector<float> result;

    if (!v.isArray())
        throw std::runtime_error(std::string(key) + " value must be Array.");

    for (unsigned int i = 0; i < size; i++) {
        auto element = v[i];
        if (element.isNull())
            throw std::runtime_error(std::string(key) + " element value must not be Null.");

        if (!element.isDouble())
            throw std::runtime_error(std::string(key) + " element value must be Numeric.");

        result.push_back(element.asFloat());
    }
    return result;
}

template <>
std::vector<std::vector<std::vector<float>>> Get<std::vector<std::vector<std::vector<float>>>>(
    Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];

    if (v.isNull())
        throw std::runtime_error(std::string(key) + " first layer value must not be Null.");

    if (!v.isArray())
        throw std::runtime_error(std::string(key) + " first layer value must be Array.");

    std::vector<std::vector<std::vector<float>>> result;
    for (unsigned int i = 0; i < v.size(); i++) {
        auto v1 = v[i];
        if (v1.isNull())
            throw std::runtime_error(std::string(key) + " second layer value must not be Null.");

        if (!v1.isArray())
            throw std::runtime_error(std::string(key) + " second layer value must be Array.");

        std::vector<std::vector<float>> data_v1;
        for (unsigned int j = 0; j < v1.size(); j++) {
            auto v2 = v1[j];
            if (v2.isNull())
                throw std::runtime_error(std::string(key) + " third layer value must not be Null.");

            if (!v2.isArray())
                throw std::runtime_error(std::string(key) + " third layer value must be Array.");

            std::vector<float> data_v2;
            for (unsigned int k = 0; k < v2.size(); k++) {
                auto element = v2[k];
                if (element.isNull())
                    throw std::runtime_error(std::string(key) + " element value must not be Null.");

                if (!element.isDouble())
                    throw std::runtime_error(std::string(key) + " element value must be Numeric.");

                data_v2.push_back(element.asFloat());
            }
            data_v1.push_back(data_v2);
        }
        result.push_back(data_v1);
    }
    return result;
}

template <>
bool Get<bool>(Json::Value& value, Json::StaticString key) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        throw std::runtime_error(std::string(key) + " value must not be Null.");

    if (!v.isBool())
        throw std::runtime_error(std::string(key) + " value must be Bool.");

    return v.asBool();
}

template <>
bool Get<bool>(Json::Value& value, Json::StaticString key, bool default_value) noexcept(false) {
    Json::Value v = value[key];
    if (v.isNull())
        return default_value;
    if (!v.isBool())
        throw std::runtime_error(std::string(key) + " value must be Bool.");

    return v.asBool();
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
    char* content = new char[size];
    file.seekg(0, file.beg);
    file.read(content, size);
    file.close();
    file_content.assign(content, size);
    delete[] content;
    return COSMO_NN_OK;
}

Status ParseModelsConfig(Json::Value& config_value, CombinedModelConfig& config) {
    try {
        CheckObject(config_value, "config value must be Object.");
        // face_info
        auto face_info_value =
            Get<Json::Value>(config_value, Json::StaticString("feature_info"), Json::Value::nullSingleton());
        if (!face_info_value.isNull()) {
            CheckObject(face_info_value, "face info value must be Object.");

            config.face_info.testset_name =
                Get<std::string>(face_info_value, Json::StaticString("testset_name"), "");
            config.face_info.score_level =
                Get<std::vector<float>>(face_info_value, Json::StaticString("score_level"));
            config.face_info.cmp_score =
                Get<std::vector<float>>(face_info_value, Json::StaticString("cmp_score"));
            config.face_info.feature_dim = Get<int>(face_info_value, Json::StaticString("feature_dim"));
        }

        // instruction
        auto instructions_value =
            Get<Json::Value>(config_value, Json::StaticString("instruction"), Json::Value::nullSingleton());
        if (instructions_value.isNull())
            return COSMO_NN_OK;

        CheckArray(instructions_value, "instruction must be Array.");
        auto instructions_size = instructions_value.size();
        for (unsigned int i = 0; i < instructions_size; i++) {
            Instruction instruction;
            auto instruction_value = instructions_value[i];
            CheckNullAndObject(instruction_value, "instruction element value must be Object.");

            instruction.output_node = Get<std::string>(instruction_value, Json::StaticString("output_node"));
            instruction.shape       = Get<DimsVector>(instruction_value, Json::StaticString("shape"));

            auto instruction_categories_value = Get<Json::Value>(
                instruction_value, Json::StaticString("categories"), Json::Value::nullSingleton());
            if (!instruction_categories_value.isNull()) {
                CheckArray(instruction_categories_value, "categories must be Array.");
                auto instruction_categories_size = instruction_categories_value.size();
                for (unsigned int i = 0; i < instruction_categories_size; i++) {
                    Json::Value instruction_categories_element_value = instruction_categories_value[i];
                    CheckNullAndObject(instruction_categories_element_value,
                                       "instruction categories element must be Object.");
                    CategoryInfo category_info;
                    category_info.class_name = Get<std::string>(instruction_categories_element_value,
                                                                Json::StaticString("class_name"));
                    category_info.split =
                        Get<int>(instruction_categories_element_value, Json::StaticString("split"));
                    category_info.threshold = Get<std::vector<float>>(instruction_categories_element_value,
                                                                      Json::StaticString("threshold"));
                    instruction.categories.emplace_back(category_info);
                }
            }

            auto instruction_output_infos_value =
                Get<Json::Value>(instruction_value, Json::StaticString("output_info"));
            CheckArray(instruction_output_infos_value, "instruction output info must be Array");
            auto instruction_output_info_size = instruction_output_infos_value.size();
            instruction.infos.resize(instruction_output_info_size);
            for (unsigned int i = 0; i < instruction_output_info_size; i++) {
                auto instruction_output_info_value = instruction_output_infos_value[i];
                CheckNullAndObject(instruction_output_info_value, "output_info must be Object");

                instruction.infos.at(i).label =
                    Get<std::string>(instruction_output_info_value, Json::StaticString("label"));
                instruction.infos.at(i).class_name =
                    Get<std::string>(instruction_output_info_value, Json::StaticString("class_name"));
                instruction.infos.at(i).thresholds =
                    Get<std::vector<float>>(instruction_output_info_value, Json::StaticString("threshold"));
            }

            config.instructions.emplace_back(instruction);
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputOp(Json::Value& op_value, Op** op_ptr) {
    try {
        auto op_name = Get<std::string>(op_value, Json::StaticString("op"));
        // todo: add more ops
        if (op_name == "resize") {
            Resize* resize  = new Resize();
            resize->dsize   = Get<std::vector<int>>(op_value, Json::StaticString("dsize"));
            resize->gravity = Get<int>(op_value, Json::StaticString("gravity"),
                                       Get<int>(op_value, Json::StaticString("padding_gravity"), 0));
            resize->color   = Get<std::vector<int>>(op_value, Json::StaticString("color"));
            resize->skip    = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr         = resize;
            return COSMO_NN_OK;
        } else if (op_name == "normalize") {
            Normalize* normalize = new Normalize();
            normalize->mean      = Get<std::vector<float>>(op_value, Json::StaticString("mean"));
            normalize->std =
                Get<std::vector<float>>(op_value, Json::StaticString("std"), std::vector<float>());
            normalize->scale  = Get<float>(op_value, Json::StaticString("scale"));
            normalize->is_bgr = Get<bool>(op_value, Json::StaticString("is_bgr"));
            normalize->skip   = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr           = normalize;
            return COSMO_NN_OK;
        } else if (op_name == "crop" || op_name == "expand") {
            RectCrop* rect_crop = new RectCrop();
            rect_crop->type     = op_name;

            // Unified signed ratio [-1, 1]: negative or 0 means crop on that side, positive means expand
            // (computed by sign at runtime)
            float top    = Get<float>(op_value, Json::StaticString("h_top_crop"), 0.0f);
            float bottom = Get<float>(op_value, Json::StaticString("h_bottom_crop"), 0.0f);
            float left   = Get<float>(op_value, Json::StaticString("w_left_crop"), 0.0f);
            float right  = Get<float>(op_value, Json::StaticString("w_right_crop"), 0.0f);

            rect_crop->h_top_crop    = {top};
            rect_crop->h_bottom_crop = {bottom};
            rect_crop->w_left_crop   = {left};
            rect_crop->w_right_crop  = {right};
            rect_crop->bbox_hw_ratio_levels.clear();

            rect_crop->square      = Get<bool>(op_value, Json::StaticString("square"), false);
            rect_crop->square_mode = Get<int>(op_value, Json::StaticString("square_mode"), 0);
            rect_crop->skip        = Get<bool>(op_value, Json::StaticString("skip"), false);

            *op_ptr = rect_crop;
            return COSMO_NN_OK;
        } else if (op_name == "affine_crop") {
            AffineCrop* affine = new AffineCrop();
            affine->norm_mode  = Get<int>(op_value, Json::StaticString("NormMode"));
            if (affine->norm_mode < 0 || affine->norm_mode > 2)
                return Status(COSMO_NN_ERR_JSON_PARSE, "affine NormMode tag must be in [0, 2].");

            affine->norm_ratio   = Get<float>(op_value, Json::StaticString("norm_ratio"));
            affine->output_hw    = Get<std::vector<int>>(op_value, Json::StaticString("output_hw"));
            affine->center_index = Get<std::vector<int>>(op_value, Json::StaticString("center_index"));
            affine->skip         = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr              = affine;
            return COSMO_NN_OK;
        } else if (op_name == "sequence") {
            Sequence* sequence = new Sequence();
            sequence->size     = Get<int>(op_value, Json::StaticString("size"));
            sequence->scale    = Get<float>(op_value, Json::StaticString("scale"));
            sequence->dsize    = Get<std::vector<int>>(op_value, Json::StaticString("dsize"));
            sequence->is_bgr   = Get<bool>(op_value, Json::StaticString("is_bgr"));
            sequence->skip     = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr            = sequence;
            return COSMO_NN_OK;
        } else if (op_name == "combine_image") {
            auto combine_image        = new CombineImage();
            combine_image->count      = Get<int>(op_value, Json::StaticString("count"));
            combine_image->dst_height = Get<int>(op_value, Json::StaticString("dst_height"));
            combine_image->dst_width  = Get<int>(op_value, Json::StaticString("dst_width"));
            combine_image->skip       = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr                   = combine_image;
            return COSMO_NN_OK;
        } else if (op_name == "dino_encode") {
            auto dino_encode        = new DinoEncoder();
            dino_encode->dst_height = Get<int>(op_value, Json::StaticString("dst_height"));
            dino_encode->dst_width  = Get<int>(op_value, Json::StaticString("dst_width"));
            dino_encode->is_bgr     = Get<bool>(op_value, Json::StaticString("is_bgr"));
            dino_encode->mean       = Get<std::vector<float>>(op_value, Json::StaticString("mean"));
            dino_encode->std        = Get<std::vector<float>>(op_value, Json::StaticString("std"));
            dino_encode->skip       = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr                 = dino_encode;
            return COSMO_NN_OK;
        } else if (op_name == "sam_prompt_encode") {
            auto sam_prompt_encode = new SAMPromptEncode();
            sam_prompt_encode->prompt_type =
                Get<std::string>(op_value, Json::StaticString("prompt_type"), "point");
            sam_prompt_encode->normalize    = Get<bool>(op_value, Json::StaticString("normalize"), true);
            sam_prompt_encode->encoder_size = Get<int>(op_value, Json::StaticString("encoder_size"), 1024);
            sam_prompt_encode->max_points   = Get<int>(op_value, Json::StaticString("max_points"), 6);
            sam_prompt_encode->skip         = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr                         = sam_prompt_encode;
            return COSMO_NN_OK;
        } else {
            return Status(COSMO_NN_ERR_JSON_PARSE, "Unsupport input op.");
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelOutputOp(Json::Value& op_value, Op** op_ptr) {
    try {
        CheckObject(op_value, "output post_process node must be Object.");
        auto op_name = Get<std::string>(op_value, Json::StaticString("op"));
        // todo: add more post process op
        if (op_name == "yolo_postprocess") {
            YoloPost* yolo_post           = new YoloPost();
            yolo_post->nms_threshold      = Get<float>(op_value, Json::StaticString("nms_threshold"));
            yolo_post->nms_detection_conf = Get<float>(op_value, Json::StaticString("nms_detection_conf"));
            yolo_post->top_k              = Get<int>(op_value, Json::StaticString("top_k"));
            *op_ptr                       = yolo_post;
            return COSMO_NN_OK;
        } else if (op_name == "yolo_npu_postprocess") {
            YoloNpuPost* yolo_npu_post   = new YoloNpuPost();
            yolo_npu_post->nms_threshold = Get<float>(op_value, Json::StaticString("nms_threshold"));
            yolo_npu_post->nms_detection_conf =
                Get<float>(op_value, Json::StaticString("nms_detection_conf"));
            yolo_npu_post->top_k = Get<int>(op_value, Json::StaticString("top_k"));
            yolo_npu_post->anchors =
                Get<std::vector<std::vector<std::vector<float>>>>(op_value, Json::StaticString("anchors"));
            yolo_npu_post->stride = Get<std::vector<float>>(op_value, Json::StaticString("stride"));
            std::string des       = yolo_npu_post->Description();
            *op_ptr               = yolo_npu_post;
            return COSMO_NN_OK;
        } else if (op_name == "yolov8_postprocess") {
            // YOLOv8 uses the same output format as YOLOv5 when anchor decoding is in the model
            YoloPost* yolov8_post           = new YoloPost("yolov8_postprocess");
            yolov8_post->nms_threshold      = Get<float>(op_value, Json::StaticString("nms_threshold"));
            yolov8_post->nms_detection_conf = Get<float>(op_value, Json::StaticString("nms_detection_conf"));
            yolov8_post->top_k              = Get<int>(op_value, Json::StaticString("top_k"));
            *op_ptr                         = yolov8_post;
            return COSMO_NN_OK;
        } else if (op_name == "sum") {
            Sum* s  = new Sum();
            *op_ptr = s;
            return COSMO_NN_OK;
        } else if (op_name == "arg_max") {
            ArgMax* arg_max = new ArgMax();
            arg_max->axis   = Get<int>(op_value, Json::StaticString("axis"));
            *op_ptr         = arg_max;
            return COSMO_NN_OK;
        } else if (op_name == "split") {
            Split* split = new Split();
            split->axis  = Get<int>(op_value, Json::StaticString("axis"));
            split->split = Get<std::vector<int>>(op_value, Json::StaticString("split"));
            *op_ptr      = split;
            return COSMO_NN_OK;
        } else if (op_name == "split_arg_max") {
            SplitArgMax* split_arg_max = new SplitArgMax();
            split_arg_max->split       = Get<DimsVector>(op_value, Json::StaticString("split"));
            *op_ptr                    = split_arg_max;
            return COSMO_NN_OK;
        } else if (op_name == "dino_decode") {
            DinoDecode* dino_decode     = new DinoDecode();
            dino_decode->text_threshold = Get<float>(op_value, Json::StaticString("text_threshold"));
            dino_decode->box_threshold  = Get<float>(op_value, Json::StaticString("box_threshold"));
            *op_ptr                     = dino_decode;
            return COSMO_NN_OK;
        } else if (op_name == "sam_decode") {
            SAMDecode* sam_decode = new SAMDecode();
            sam_decode->threshold = Get<float>(op_value, Json::StaticString("threshold"), 0.0f);
            sam_decode->output_size =
                Get<std::vector<int>>(op_value, Json::StaticString("output_size"), std::vector<int>());
            sam_decode->skip = Get<bool>(op_value, Json::StaticString("skip"), false);
            *op_ptr          = sam_decode;
            return COSMO_NN_OK;
        } else {
            return Status(COSMO_NN_ERR_JSON_PARSE, "Unsupport output op");
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputOps(Json::Value& ops_value, std::vector<Op*>& ops) {
    try {
        auto input_node_ops_size = ops_value.size();
        if (input_node_ops_size < 1)
            return Status(COSMO_NN_ERR_JSON_PARSE, "ops can not be empty.");

        ops.resize(input_node_ops_size);
        for (unsigned int i = 0; i < input_node_ops_size; i++) {
            Json::Value op_value = ops_value[i];
            CheckNullAndObject(op_value, "preprocess element must not be null and must be Object.");
            RETURN_ON_FAIL(ParseModelInputOp(op_value, &(ops.at(i))));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputNode(Json::Value& input_node_info_value, InputNodeInfo& input_node) {
    try {
        input_node.name  = Get<std::string>(input_node_info_value, Json::StaticString("input_node"));
        input_node.shape = Get<DimsVector>(input_node_info_value, Json::StaticString("shape"));

        input_node.data_type      = Get<int>(input_node_info_value, Json::StaticString("data_type"));
        auto input_node_ops_value = Get<Json::Value>(input_node_info_value, Json::StaticString("preprocess"),
                                                     Json::Value::nullSingleton());
        if (!input_node_ops_value.isNull()) {
            CheckArray(input_node_ops_value, "input node ops must be Array.");
            RETURN_ON_FAIL(ParseModelInputOps(input_node_ops_value, input_node.ops));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInputNodes(Json::Value& input_node_infos_value,
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

Status ParseModelOutputNode(Json::Value& output_node_value, OutputNodeInfo& output_node) {
    try {
        output_node.name      = Get<std::string>(output_node_value, Json::StaticString("output_node"));
        output_node.shape     = Get<DimsVector>(output_node_value, Json::StaticString("shape"));
        output_node.data_type = Get<int>(output_node_value, Json::StaticString("data_type"));
        auto output_node_post_process_value = Get<Json::Value>(
            output_node_value, Json::StaticString("post_process"), Json::Value::nullSingleton());
        if (!output_node_post_process_value.isNull())
            RETURN_ON_FAIL(ParseModelOutputOp(output_node_post_process_value, &(output_node.op)));
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelOutputNodes(Json::Value& output_node_infos_value,
                             std::vector<OutputNodeInfo>& output_node_infos) {
    try {
        auto output_node_size = output_node_infos_value.size();
        if (output_node_size < 1)
            return Status(COSMO_NN_ERR_JSON_PARSE, "output_node_infos must have at least one node.");

        output_node_infos.resize(output_node_size);
        for (unsigned int i = 0; i < output_node_size; i++) {
            Json::Value output_node_value = output_node_infos_value[i];
            CheckNullAndObject(output_node_value, "output_node_infos element must be Object.");
            RETURN_ON_FAIL(ParseModelOutputNode(output_node_value, output_node_infos.at(i)));
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }
    return COSMO_NN_OK;
}

Status ParseModelInfoInner(Json::Value& info_value, ModelInfo& model_info) {
    try {
        model_info.description      = Get<std::string>(info_value, Json::StaticString("description"), "");
        model_info.name             = Get<std::string>(info_value, Json::StaticString("name"));
        model_info.filename         = Get<std::string>(info_value, Json::StaticString("file_name"));
        model_info.file_md5         = Get<std::string>(info_value, Json::StaticString("file_MD5"), "");
        model_info.max_batch        = Get<int>(info_value, Json::StaticString("max_batch"));
        auto input_node_infos_value = Get<Json::Value>(info_value, Json::StaticString("inputs"));
        CheckArray(input_node_infos_value, "inputs must be Array.");
        RETURN_ON_FAIL(ParseModelInputNodes(input_node_infos_value, model_info.input_node_infos));

        auto output_node_infos_value = Get<Json::Value>(info_value, Json::StaticString("outputs"));
        CheckArray(output_node_infos_value, "outputs must be Array.");
        RETURN_ON_FAIL(ParseModelOutputNodes(output_node_infos_value, model_info.output_node_infos));
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ParseModelsInfo(Json::Value& model_value, std::vector<ModelInfo>& models) {
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

Status ParseModelConvert(Json::Value& convert_value, Convert& convert) {
    try {
        CheckObject(convert_value, "config value must be Object.");
        convert.type      = Get<std::string>(convert_value, Json::StaticString("type"));
        convert.max_batch = Get<int>(convert_value, Json::StaticString("max_batch"));
        convert.precision = Get<std::string>(convert_value, Json::StaticString("precision"));
        auto models_value =
            Get<Json::Value>(convert_value, Json::StaticString("model"), Json::Value::nullSingleton());
        if (models_value.isNull())
            return COSMO_NN_OK;

        CheckArray(models_value, "model must be Array.");
        convert.models.resize(models_value.size());
        for (unsigned int i = 0; i < models_value.size(); i++) {
            auto model_value = models_value[i];
            CheckNullAndObject(model_value, "inner must be Object.");
            convert.models.at(i).mean  = Get<std::vector<float>>(model_value, Json::StaticString("mean"), {});
            convert.models.at(i).scale = Get<float>(model_value, Json::StaticString("scale"), 0.f);
            convert.models.at(i).is_bgr = Get<bool>(model_value, Json::StaticString("is_bgr"), false);
            convert.models.at(i).is_opconvert =
                Get<bool>(model_value, Json::StaticString("is_opconvert"), true);
            convert.models.at(i).is_optimize =
                Get<bool>(model_value, Json::StaticString("is_optimize"), true);
            convert.models.at(i).is_normalize =
                Get<bool>(model_value, Json::StaticString("is_normalize"), false);
        }
    } catch (std::exception& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ModelInfoUtils::ParseModelInfo(const std::string& info_content_, CombinedModelInfo& info) {
    const auto rawJsonLength = static_cast<int>(info_content_.length());

    JSONCPP_STRING err;
    Json::Value root;

    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

    if (!reader->parse(info_content_.c_str(), info_content_.c_str() + rawJsonLength, &root, &err)) {
        return Status(COSMO_NN_ERR_JSON_PARSE, err);
    }

    try {
        CheckNullAndObject(root, "root must be Object.");
        info.reduce        = Get<std::string>(root, Json::StaticString("reduce"), "");
        info.type          = Get<std::string>(root, Json::StaticString("type"));
        info.algorithmcode = Get<std::string>(root, Json::StaticString("algorithmcode"));
        auto model_value   = Get<Json::Value>(root, Json::StaticString("model"));
        RETURN_ON_FAIL(ParseModelsInfo(model_value, info.models));

        auto config_value =
            Get<Json::Value>(root, Json::StaticString("config"), Json::Value::nullSingleton());
        if (!config_value.isNull())
            RETURN_ON_FAIL(ParseModelsConfig(config_value, info.config));

        auto convert_value =
            Get<Json::Value>(root, Json::StaticString("cwnn_convert"), Json::Value::nullSingleton());
        if (!convert_value.isNull())
            RETURN_ON_FAIL(ParseModelConvert(convert_value, info.convert));

    } catch (const std::runtime_error& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    } catch (const std::invalid_argument& e) {
        return Status(COSMO_NN_ERR_JSON_PARSE, e.what());
    }

    return COSMO_NN_OK;
}

Status ModelInfoUtils::GetInputShapesMap(const ModelInfo& model_info_, ShapesMap& shapes) {
    auto inputs_node_info = model_info_.input_node_infos;
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
