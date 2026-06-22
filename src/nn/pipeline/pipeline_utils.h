#pragma once

#include <memory>
#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "nn/core/status.h"
#include "nn/pipeline/model_pipeline.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {
namespace pipeline_utils {

    std::unique_ptr<Resize> MakeResizeOp(const std::vector<int>& dsize, int gravity = 0,
                                         const std::vector<int>& color = {114, 114, 114});

    std::unique_ptr<Normalize> MakeNormalizeOp(const std::vector<float>& mean, float scale,
                                               bool is_bgr = true, const std::vector<float>& std_dev = {});

    std::unique_ptr<AffineCrop> MakeAffineCropOp(float norm_ratio, int norm_mode,
                                                 const std::vector<int>& output_hw,
                                                 const std::vector<int>& center_index);

    std::unique_ptr<CropResize> MakeCropResizeOp(const std::string& type,
                                                 const std::vector<float>& h_top_crop,
                                                 const std::vector<float>& h_bottom_crop,
                                                 const std::vector<float>& w_left_crop,
                                                 const std::vector<float>& w_right_crop, bool square,
                                                 int square_mode, const std::vector<int>& dsize, int gravity,
                                                 const std::vector<int>& color);

    std::unique_ptr<Sequence> MakeSequenceOp(int size, float scale, const std::vector<int>& dsize,
                                             bool is_bgr);

    std::unique_ptr<YoloPost> MakeYoloPostOp(float nms_threshold, float nms_detection_conf, int top_k,
                                             int input_width = 0, int input_height = 0);

    std::unique_ptr<YoloNpuPost> MakeYoloNpuPostOp(
        float nms_threshold, float nms_detection_conf, int top_k,
        const std::vector<std::vector<std::vector<float>>>& anchors, const std::vector<float>& stride);

    std::unique_ptr<YoloPost> MakeYoloV8PostOp(float nms_threshold, float nms_detection_conf, int top_k,
                                               int input_width = 0, int input_height = 0);

    std::unique_ptr<YoloPost> MakeYoloE2EPostOp(float conf_threshold, int top_k, int input_width = 0,
                                                int input_height = 0);

    std::unique_ptr<DinoEncoder> MakeDinoEncoderOp(int dst_width, int dst_height, bool is_bgr,
                                                   const std::vector<float>& mean,
                                                   const std::vector<float>& std_dev);

    std::unique_ptr<DinoDecode> MakeDinoDecodeOp(float text_threshold, float box_threshold);

    std::unique_ptr<SAMPromptEncode> MakeSAMPromptEncodeOp(const std::string& prompt_type, bool normalize,
                                                           int encoder_size, int max_points);

    std::unique_ptr<SAMDecode> MakeSAMDecodeOp(float threshold, const std::vector<int>& output_size);

    void BuildInstructionsFromLabels(const std::vector<PipelineLabelInfo>& labels,
                                     const std::string& output_node_name, const DimsVector& output_shape,
                                     CombinedModelConfig& config);

    nlohmann::json ParseJsonObject(const std::string& raw);

    std::string ReadString(const nlohmann::json& json, const char* key, std::string defaults = {});

    int ReadInt(const nlohmann::json& json, const char* key, int defaults = 0);

    size_t ReadSize(const nlohmann::json& json, const char* key, size_t defaults = 0);

    float ReadFloat(const nlohmann::json& json, const char* key, float defaults = 0.0f);

    bool ReadBool(const nlohmann::json& json, const char* key, bool defaults = false);

    std::vector<int> ReadIntArray(const nlohmann::json& json, const char* key,
                                  std::vector<int> defaults = {}, size_t min_size = 0);

    std::vector<float> ReadFloatArray(const nlohmann::json& json, const char* key,
                                      std::vector<float> defaults = {}, size_t min_size = 0);

    std::vector<std::vector<std::vector<float>>> ReadFloat3DArray(
        const nlohmann::json& json, const char* key,
        std::vector<std::vector<std::vector<float>>> defaults = {}, size_t min_outer_size = 0,
        size_t min_middle_size = 0, size_t min_inner_size = 0);

    Status ParsePipelineConfig(const std::string& json_content, PipelineConfig& config);

}  // namespace pipeline_utils
}  // namespace cosmo::nn
