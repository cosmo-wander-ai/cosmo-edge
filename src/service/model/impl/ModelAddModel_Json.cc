// ModelAddModel_Json.cc — Template update helpers for ModelImportExporter.
// Split from ModelAddModel.cc to reduce file size (DEBT-001).

// clang-format off
#include "service/model/impl/ModelImportExporter.h"
// clang-format on

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"
#include "util/Log.h"
#include "util/NnBackendConstants.h"

namespace cosmo::service {

namespace {

    constexpr int kMaxDefaultLabelCount = 80;

    bool IsDetectionModel(const std::string& modelType) {
        return modelType.size() >= 4 && modelType.compare(modelType.size() - 4, 4, "_det") == 0;
    }

    bool IsClassifyModel(const std::string& modelType) {
        return modelType == "classify";
    }

    std::vector<int> ReadShape(const nlohmann::json& node) {
        std::vector<int> shape;
        if (!node.contains("shape") || !node["shape"].is_array())
            return shape;

        for (const auto& item : node["shape"]) {
            if (item.is_number_integer())
                shape.push_back(item.get<int>());
        }
        return shape;
    }

    double ReadDefaultThreshold(const std::string& modelType) {
        if (IsDetectionModel(modelType))
            return 0.25;
        return 0.5;
    }

    int InferClassCountFromShape(const std::string& modelType, const std::vector<int>& shape) {
        if (shape.empty())
            return 0;

        // End-to-end decoded output [1, top_k, 6/7] carries no class-count info;
        // labels must be provided explicitly at import time.
        if ((modelType == "yolo26_det" || modelType == "yolo26_obb_det") && shape.size() == 3 &&
            (shape[2] == 6 || shape[2] == 7))
            return 0;

        const auto class_field_count = [&]() {
            if (modelType == "yolov5_det" || modelType == "yolo26_det")
                return 5;
            if (modelType == "yolo26_obb_det")
                return 6;  // 4 box + 1 score + 1 angle
            return 4;
        };

        const auto class_count_from_dim = [](int dim, int class_fields) {
            return dim > class_fields ? dim - class_fields : 0;
        };

        if (IsClassifyModel(modelType)) {
            const int last_dim = shape.back();
            if (last_dim <= 0)
                return 0;
            return last_dim;
        }

        if (!IsDetectionModel(modelType))
            return 0;

        const int class_fields = class_field_count();
        int candidate_dim      = 0;
        for (size_t i = 0; i < shape.size(); ++i) {
            if (class_count_from_dim(shape[i], class_fields) <= 0)
                continue;
            if (candidate_dim == 0 || shape[i] < candidate_dim)
                candidate_dim = shape[i];
        }
        return class_count_from_dim(candidate_dim, class_fields);
    }

    int InferClassCount(const nlohmann::json& doc, const std::string& modelType) {
        if (!IsDetectionModel(modelType) && !IsClassifyModel(modelType))
            return 0;
        if (!doc.contains("models") || !doc["models"].is_array() || doc["models"].empty())
            return 0;

        const auto& modelObj = doc["models"][0];
        if (!modelObj.contains("outputs") || !modelObj["outputs"].is_array() || modelObj["outputs"].empty())
            return 0;

        for (const auto& output : modelObj["outputs"]) {
            const int class_count = InferClassCountFromShape(modelType, ReadShape(output));
            if (class_count > 0)
                return class_count;
        }
        return 0;
    }

    void AppendJsonCodeUnit(std::string& encoded, uint32_t value) {
        constexpr char hex[] = "0123456789abcdef";
        encoded += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4)
            encoded += hex[(value >> shift) & 0xf];
    }

    // Read one Python repr / JSON string without evaluating expressions. Normalize
    // Python-only escapes to JSON, whose parser also validates UTF-8/surrogate pairs.
    bool ReadClassNameString(const std::string& raw, size_t& position, std::string& value) {
        if (position >= raw.size() || (raw[position] != '\'' && raw[position] != '"'))
            return false;
        const char quote    = raw[position++];
        std::string encoded = "\"";
        while (position < raw.size()) {
            const char current = raw[position++];
            if (current == quote) {
                encoded += '"';
                const auto decoded = nlohmann::json::parse(encoded, nullptr, false);
                if (!decoded.is_string())
                    return false;
                value = decoded.get<std::string>();
                return true;
            }
            if (current != '\\') {
                if (static_cast<unsigned char>(current) < 0x20)
                    return false;
                if (current == '"')
                    encoded += '\\';
                encoded += current;
                continue;
            }
            if (position == raw.size())
                return false;
            const char escape = raw[position++];
            switch (escape) {
                case '\'':
                    encoded += '\'';
                    break;
                case '"':
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                    encoded += '\\';
                    encoded += escape;
                    break;
                case 'a':
                case 'v':
                    AppendJsonCodeUnit(encoded, escape == 'a' ? 7 : 11);
                    break;
                case 'x':
                case 'u':
                case 'U': {
                    const size_t digits = escape == 'x' ? 2 : (escape == 'u' ? 4 : 8);
                    if (raw.size() - position < digits)
                        return false;
                    uint32_t code_point = 0;
                    for (size_t i = 0; i < digits; ++i) {
                        const char digit = raw[position++];
                        const int value  = digit >= '0' && digit <= '9'   ? digit - '0'
                                           : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                                           : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                                                          : -1;
                        if (value < 0)
                            return false;
                        code_point = (code_point << 4) | static_cast<uint32_t>(value);
                    }
                    if (code_point > 0x10ffff ||
                        (escape == 'U' && code_point >= 0xd800 && code_point <= 0xdfff))
                        return false;
                    if (code_point > 0xffff) {
                        code_point -= 0x10000;
                        AppendJsonCodeUnit(encoded, 0xd800 + (code_point >> 10));
                        AppendJsonCodeUnit(encoded, 0xdc00 + (code_point & 0x3ff));
                    } else {
                        AppendJsonCodeUnit(encoded, code_point);
                    }
                    break;
                }
                default: {
                    if (escape < '0' || escape > '7')
                        return false;
                    uint32_t code_point = static_cast<uint32_t>(escape - '0');
                    for (int i = 1;
                         i < 3 && position < raw.size() && raw[position] >= '0' && raw[position] <= '7';
                         ++i) {
                        code_point = (code_point << 3) | static_cast<uint32_t>(raw[position++] - '0');
                    }
                    AppendJsonCodeUnit(encoded, code_point);
                    break;
                }
            }
        }
        return false;
    }

    // Accept a complete names dictionary, including Python repr and JSON object
    // keys. Never publish a partial regex match as the model's full class list.
    std::vector<std::pair<int, std::string>> ParseOnnxClassNames(const std::string& raw) {
        std::vector<std::pair<int, std::string>> names;
        size_t position       = 0;
        const auto skip_space = [&]() {
            while (position < raw.size() && (raw[position] == ' ' || raw[position] == '\t' ||
                                             raw[position] == '\r' || raw[position] == '\n'))
                ++position;
        };
        const auto consume = [&](char expected) {
            skip_space();
            if (position == raw.size() || raw[position] != expected)
                return false;
            ++position;
            return true;
        };
        if (!consume('{'))
            return {};
        skip_space();
        while (position < raw.size() && raw[position] != '}') {
            std::string id_string;
            if (raw[position] == '\'' || raw[position] == '"') {
                if (!ReadClassNameString(raw, position, id_string))
                    return {};
            } else {
                const size_t start = position;
                while (position < raw.size() && raw[position] >= '0' && raw[position] <= '9')
                    ++position;
                id_string = raw.substr(start, position - start);
                if (id_string.size() > 1 && id_string[0] == '0')
                    return {};
            }
            if (id_string.empty() ||
                !std::all_of(id_string.begin(), id_string.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return {};
            int id            = 0;
            const auto parsed = std::from_chars(id_string.data(), id_string.data() + id_string.size(), id);
            if (parsed.ec != std::errc{} || parsed.ptr != id_string.data() + id_string.size() ||
                !consume(':'))
                return {};
            skip_space();
            std::string name;
            if (!ReadClassNameString(raw, position, name))
                return {};
            names.emplace_back(id, std::move(name));
            skip_space();
            if (position < raw.size() && raw[position] == '}')
                break;
            if (!consume(','))
                return {};
            skip_space();
        }
        if (!consume('}'))
            return {};
        skip_space();
        if (position != raw.size())
            return {};
        std::sort(names.begin(), names.end());
        // Ultralytics class indices are consecutive, starting at zero. This
        // also rejects duplicates, missing entries and out-of-range class IDs.
        for (size_t i = 0; i < names.size(); ++i) {
            if (static_cast<size_t>(names[i].first) != i)
                return {};
        }
        return names;
    }

    // End-to-end outputs ([1, top_k, 6/7]) carry no class-count info, so class
    // names are taken from ONNX metadata when available (Ultralytics exports
    // always embed them under the "names" key). Returns true when labels were
    // filled, in which case the shape-based default fill must be skipped.
    bool FillLabelsFromOnnxMetadata(nlohmann::json& doc, const std::string& modelType,
                                    const std::vector<cosmo::BmodelInfo>& bmodel_infos) {
        if (!IsDetectionModel(modelType))
            return false;
        for (const auto& info : bmodel_infos) {
            if (!info.valid || info.class_names_raw.empty())
                continue;
            auto names = ParseOnnxClassNames(info.class_names_raw);
            if (names.empty()) {
                LOG_WARN("[AddModel] Invalid ONNX class names for {}; using existing default-label path",
                         modelType);
                continue;
            }
            const double threshold = ReadDefaultThreshold(modelType);
            nlohmann::json labels  = nlohmann::json::array();
            for (const auto& [id, name] : names) {
                const std::string id_str = std::to_string(id);
                labels.push_back({{"id", id_str}, {"name", name}, {"threshold", {threshold, threshold}}});
            }
            doc["labels"] = labels;
            LOG_INFO("[AddModel] Filled {} labels from ONNX metadata names for model type {}", names.size(),
                     modelType);
            return true;
        }
        return false;
    }

    void FillDefaultLabels(nlohmann::json& doc, const std::string& modelType) {
        const int class_count = InferClassCount(doc, modelType);
        if (class_count <= 0)
            return;
        const int label_count = std::min(class_count, kMaxDefaultLabelCount);

        const double threshold = ReadDefaultThreshold(modelType);

        nlohmann::json labels = nlohmann::json::array();
        for (int i = 0; i < label_count; ++i) {
            const std::string id   = std::to_string(i);
            const std::string name = "category" + id;
            labels.push_back({{"id", id}, {"name", name}, {"threshold", {threshold, threshold}}});
        }

        doc["labels"] = labels;

        LOG_INFO("[AddModel] Generated {} default labels for model type {} (inferred class count {})",
                 label_count, modelType, class_count);
    }

    struct NormalizeConfig {
        std::vector<double> mean;
        double scale{1.0};
        bool valid{false};
    };

    NormalizeConfig BuildNormalizeConfig(const std::string& normalizationMode) {
        if (normalizationMode == "0-1" || normalizationMode.empty()) {
            return {{0.0, 0.0, 0.0}, 0.00392157, true};
        }
        if (normalizationMode == "-1-1") {
            return {{127.5, 127.5, 127.5}, 0.0078125, true};
        }
        if (normalizationMode == "none") {
            return {{0.0, 0.0, 0.0}, 1.0, true};
        }
        return {};
    }

    void SetPreprocessParams(nlohmann::json& modelObj, const std::string& normalizationMode,
                             const std::string& colorChannel) {
        if (normalizationMode.empty() && colorChannel.empty())
            return;

        if (!modelObj.contains("params") || !modelObj["params"].is_object()) {
            modelObj["params"] = nlohmann::json::object();
        }

        auto& params = modelObj["params"];
        if (!normalizationMode.empty()) {
            const auto normalize = BuildNormalizeConfig(normalizationMode);
            if (normalize.valid) {
                params["normalize_mean"]  = normalize.mean;
                params["normalize_scale"] = normalize.scale;
            }
        }

        if (!colorChannel.empty()) {
            params["is_bgr"] = (colorChannel == "bgr");
        }
    }

    std::vector<int> MergeShapeWithTemplate(const nlohmann::json& node, const std::vector<int>& modelShape,
                                            bool allowTemplateFallback, bool allowBatchFallback) {
        std::vector<int> templateShape = ReadShape(node);
        std::vector<int> mergedShape;
        mergedShape.reserve(modelShape.size());

        for (size_t i = 0; i < modelShape.size(); ++i) {
            if (modelShape[i] > 0) {
                mergedShape.push_back(modelShape[i]);
            } else if ((allowTemplateFallback || (allowBatchFallback && i == 0)) &&
                       i < templateShape.size() && templateShape[i] > 0) {
                mergedShape.push_back(templateShape[i]);
            } else {
                mergedShape.push_back(modelShape[i]);
            }
        }
        return mergedShape;
    }

    nlohmann::json MakeIoNode(const cosmo::BmodelNodeInfo& nodeInfo, const nlohmann::json* templateNode,
                              bool allowTemplateShapeFallback, bool allowBatchShapeFallback) {
        nlohmann::json node = templateNode ? *templateNode : nlohmann::json::object();
        node["name"]        = nodeInfo.name;
        node["data_type"]   = nodeInfo.data_type;

        if (!nodeInfo.shape.empty()) {
            node["shape"] = MergeShapeWithTemplate(node, nodeInfo.shape, allowTemplateShapeFallback,
                                                   allowBatchShapeFallback);
        }
        return node;
    }

    void UpdateModelIONodes(nlohmann::json& modelObj, const char* section,
                            const std::vector<cosmo::BmodelNodeInfo>& io_infos) {
        if (!modelObj.contains(section) || !modelObj[section].is_array())
            return;
        if (io_infos.empty())
            return;

        auto& nodes                  = modelObj[section];
        nlohmann::json originalNodes = nodes;
        nodes                        = nlohmann::json::array();

        const bool isInput                    = std::string(section) == "inputs";
        const bool allowTemplateShapeFallback = isInput;
        const bool allowBatchShapeFallback    = !isInput;
        for (size_t j = 0; j < io_infos.size(); j++) {
            const nlohmann::json* templateNode = nullptr;
            if (j < originalNodes.size())
                templateNode = &originalNodes[j];
            else if (!originalNodes.empty())
                templateNode = &originalNodes.back();

            nodes.push_back(
                MakeIoNode(io_infos[j], templateNode, allowTemplateShapeFallback, allowBatchShapeFallback));
        }
    }

    nlohmann::json DefaultYoloV5Anchors() {
        return nlohmann::json::array(
            {nlohmann::json::array({nlohmann::json::array({10, 13}), nlohmann::json::array({16, 30}),
                                    nlohmann::json::array({33, 23})}),
             nlohmann::json::array({nlohmann::json::array({30, 61}), nlohmann::json::array({62, 45}),
                                    nlohmann::json::array({59, 119})}),
             nlohmann::json::array({nlohmann::json::array({116, 90}), nlohmann::json::array({156, 198}),
                                    nlohmann::json::array({373, 326})})});
    }

    bool IsYoloV5RawHeadOutputShape(const nlohmann::json& output) {
        const auto shape = ReadShape(output);
        return shape.size() == 5 && shape[1] == 3 && shape[4] >= 6;
    }

    void ConfigureYoloV5Postprocess(nlohmann::json& modelObj, const std::string& modelType) {
        if (modelType != "yolov5_det")
            return;
        if (!modelObj.contains("outputs") || !modelObj["outputs"].is_array())
            return;

        const auto& outputs = modelObj["outputs"];
        if (outputs.size() != 3)
            return;
        if (!std::all_of(outputs.begin(), outputs.end(), IsYoloV5RawHeadOutputShape))
            return;

        if (!modelObj.contains("params") || !modelObj["params"].is_object())
            modelObj["params"] = nlohmann::json::object();

        auto& params                  = modelObj["params"];
        params["use_npu_postprocess"] = true;
        if (!params.contains("anchors") || !params["anchors"].is_array())
            params["anchors"] = DefaultYoloV5Anchors();
        if (!params.contains("stride") || !params["stride"].is_array())
            params["stride"] = nlohmann::json::array({8, 16, 32});
    }

    void UpdateInputSize(nlohmann::json& modelObj, const std::vector<cosmo::BmodelNodeInfo>& inputs) {
        if (inputs.empty() || inputs[0].shape.size() < 4)
            return;
        if (!modelObj.contains("params") || !modelObj["params"].is_object())
            return;

        // Skip if height or width dimensions are dynamic (-1)
        if (inputs[0].shape[2] <= 0 || inputs[0].shape[3] <= 0)
            return;

        std::vector<int> input_size      = {inputs[0].shape[2], inputs[0].shape[3]};
        modelObj["params"]["input_size"] = input_size;
    }

}  // namespace

void ModelImportExporter::UpdateTemplateConfig(nlohmann::json& templateDoc, const std::string& modelCode,
                                               const std::string& versionStr, const std::string& modelName,
                                               const std::string& modelType, const std::string& description,
                                               const std::vector<cosmo::BmodelInfo>& bmodel_infos,
                                               bool use_template_defaults,
                                               const std::string& normalizationMode,
                                               const std::string& colorChannel) {
    templateDoc["algorithm_code"] = modelCode;
    templateDoc["version"]        = versionStr;
    templateDoc["chip_type"]      = cosmo::util::kEngineType;

    if (templateDoc.contains("models") && templateDoc["models"].is_array()) {
        auto& modelArray = templateDoc["models"];

        if (!use_template_defaults && !bmodel_infos.empty()) {
            for (size_t i = 0; i < modelArray.size() && i < bmodel_infos.size(); i++) {
                auto& modelObj         = modelArray[i];
                const auto& bmodelInfo = bmodel_infos[i];
                const bool isSam2      = (modelType == "sam2");

                if (isSam2 && modelArray.size() >= 2) {
                    modelObj["file_name"] = (i == 0) ? "sam2_encoder.onnx" : "sam2_decoder.onnx";
                }

                if (!isSam2 && bmodelInfo.valid && !bmodelInfo.networks.empty()) {
                    const auto& network   = bmodelInfo.networks[0];
                    modelObj["max_batch"] = network.max_batch;
                    UpdateModelIONodes(modelObj, "inputs", network.inputs);
                    UpdateInputSize(modelObj, network.inputs);
                    UpdateModelIONodes(modelObj, "outputs", network.outputs);
                    ConfigureYoloV5Postprocess(modelObj, modelType);
                }

                modelObj["description"] = description;
                modelObj["name"]        = modelName;
                SetPreprocessParams(modelObj, normalizationMode, colorChannel);
            }
        } else {
            // Use template defaults, only update description and name
            for (size_t i = 0; i < modelArray.size(); i++) {
                auto& modelObj          = modelArray[i];
                modelObj["description"] = description;
                modelObj["name"]        = modelName;
                SetPreprocessParams(modelObj, normalizationMode, colorChannel);
            }
        }
    }

    if (!FillLabelsFromOnnxMetadata(templateDoc, modelType, bmodel_infos)) {
        FillDefaultLabels(templateDoc, modelType);
    }
}

}  // namespace cosmo::service
