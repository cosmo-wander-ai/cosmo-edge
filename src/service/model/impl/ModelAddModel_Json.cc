// ModelAddModel_Json.cc — Template update helpers for ModelImportExporter.
// Split from ModelAddModel.cc to reduce file size (DEBT-001).

// clang-format off
#include "service/model/impl/ModelImportExporter.h"
// clang-format on

#include <algorithm>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "util/Log.h"
#include "util/PlatformConstants.h"

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

        const auto class_field_count = [&]() {
            if (modelType == "yolov5_det" || modelType == "yolo26_det")
                return 5;
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

    FillDefaultLabels(templateDoc, modelType);
}

}  // namespace cosmo::service
