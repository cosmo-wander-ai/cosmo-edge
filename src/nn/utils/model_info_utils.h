#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nn/core/common.h"
#include "nn/core/macros.h"
#include "nn/core/status.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

struct PUBLIC InputNodeInfo {
    std::string name = {};
    DimsVector shape = {};

    /**
     * 0    fp32
     * 1    fp16
     * 2    bfp16
     * 3    int8
     */
    int data_type = 0;

    std::vector<std::unique_ptr<Op>> ops = {};
};

struct PUBLIC OutputNodeInfo {
    std::string name       = {};
    DimsVector shape       = {};
    int data_type          = 0;
    std::unique_ptr<Op> op = nullptr;
};

struct PUBLIC ModelInfo {
    std::string description = {};
    std::string name        = {};
    std::string filename    = {};
    std::string file_md5    = {};
    int max_batch           = 0;

    std::vector<InputNodeInfo> input_node_infos   = {};
    std::vector<OutputNodeInfo> output_node_infos = {};
};

struct PUBLIC InstructionOutputInfo {
    std::string label;
    std::string class_name;
    std::vector<float> thresholds{};
};

struct PUBLIC CategoryInfo {
    std::string class_name{};
    int split = 0;
    std::vector<float> threshold{};
};

struct PUBLIC Instruction {
    std::string output_node                  = {};
    DimsVector shape                         = {};
    std::vector<CategoryInfo> categories     = {};
    std::vector<InstructionOutputInfo> infos = {};
};

struct PUBLIC FaceInfo {
    std::string testset_name{};
    std::vector<float> score_level{};
    std::vector<float> cmp_score{};
    size_t feature_dim;
};

struct PUBLIC CombinedModelConfig {
    FaceInfo face_info;
    std::vector<Instruction> instructions = {};
};

struct PUBLIC ConvertInner {
    std::vector<float> mean{};
    float scale;
    bool is_bgr       = false;
    bool is_opconvert = true;
    bool is_optimize  = true;
    bool is_normalize = false;
};
struct PUBLIC Convert {
    std::string type{};
    int max_batch;
    std::string precision{};
    std::vector<ConvertInner> models{};
};

struct PUBLIC CombinedModelInfo {
    std::string algorithmcode     = {};
    std::string reduce            = {};
    std::string type              = {};
    std::vector<ModelInfo> models = {};
    CombinedModelConfig config;
    Convert convert;
};

class PUBLIC ModelInfoUtils {
public:
    static Status LoadJson(const std::string& json_path, std::string& content);

    static Status ParseModelInfo(const std::string& info_content_, CombinedModelInfo& info);

    static Status GetInputShapesMap(const ModelInfo& model_info_, ShapesMap& shapes);

    static void GetSelectedThreshold(const CombinedModelInfo& info,
                                     std::vector<std::vector<float>>& thresholds, int index = 0);

    static void GetSelectedClassName(const CombinedModelInfo& info,
                                     std::vector<std::vector<std::string>>& names);

    static void GetSelectedIndex(const CombinedModelInfo& info, std::vector<int>& indices);
};

}  // namespace cosmo::nn
