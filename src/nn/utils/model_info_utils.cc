#include "nn/utils/model_info_utils.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

namespace cosmo::nn {

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
