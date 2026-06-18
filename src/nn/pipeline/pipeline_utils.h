#pragma once

#include <string>
#include <vector>

#include "nn/core/status.h"
#include "nn/pipeline/model_pipeline.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {
namespace pipeline_utils {

    Resize* MakeResizeOp(const std::vector<int>& dsize, int gravity = 0,
                         const std::vector<int>& color = {114, 114, 114});

    Normalize* MakeNormalizeOp(const std::vector<float>& mean, float scale, bool is_bgr = true,
                               const std::vector<float>& std_dev = {});

    AffineCrop* MakeAffineCropOp(float norm_ratio, int norm_mode, const std::vector<int>& output_hw,
                                 const std::vector<int>& center_index);

    CropResize* MakeCropResizeOp(const std::string& type, const std::vector<float>& h_top_crop,
                                 const std::vector<float>& h_bottom_crop,
                                 const std::vector<float>& w_left_crop,
                                 const std::vector<float>& w_right_crop, bool square, int square_mode,
                                 const std::vector<int>& dsize, int gravity, const std::vector<int>& color);

    Sequence* MakeSequenceOp(int size, float scale, const std::vector<int>& dsize, bool is_bgr);

    YoloPost* MakeYoloPostOp(float nms_threshold, float nms_detection_conf, int top_k, int input_width = 0,
                             int input_height = 0);

    YoloNpuPost* MakeYoloNpuPostOp(float nms_threshold, float nms_detection_conf, int top_k,
                                   const std::vector<std::vector<std::vector<float>>>& anchors,
                                   const std::vector<float>& stride);

    YoloPost* MakeYoloV8PostOp(float nms_threshold, float nms_detection_conf, int top_k, int input_width = 0,
                               int input_height = 0);

    YoloPost* MakeYoloE2EPostOp(float conf_threshold, int top_k, int input_width = 0, int input_height = 0);

    DinoEncoder* MakeDinoEncoderOp(int dst_width, int dst_height, bool is_bgr, const std::vector<float>& mean,
                                   const std::vector<float>& std_dev);

    DinoDecode* MakeDinoDecodeOp(float text_threshold, float box_threshold);

    SAMPromptEncode* MakeSAMPromptEncodeOp(const std::string& prompt_type, bool normalize, int encoder_size,
                                           int max_points);

    SAMDecode* MakeSAMDecodeOp(float threshold, const std::vector<int>& output_size);

    void BuildInstructionsFromLabels(const std::vector<PipelineLabelInfo>& labels,
                                     const std::string& output_node_name, const DimsVector& output_shape,
                                     CombinedModelConfig& config);

    Status ParsePipelineConfig(const std::string& json_content, PipelineConfig& config);

}  // namespace pipeline_utils
}  // namespace cosmo::nn
