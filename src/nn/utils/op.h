#pragma once

#include <string>
#include <vector>

#include "nn/core/macros.h"

namespace cosmo::nn {

class PUBLIC Op {
public:
    explicit Op(const std::string name_);
    virtual ~Op();

    virtual std::string Description() = 0;

    std::string name = {};
    bool skip        = false;
};

class PUBLIC UnknownOp : public Op {
public:
    explicit UnknownOp(const std::string name_ = "Unknown");
    virtual ~UnknownOp();

    virtual std::string Description() override;
};

class PUBLIC Resize : public Op {
public:
    explicit Resize(const std::string name_ = "resize");
    virtual ~Resize();

    virtual std::string Description() override;

    // height width
    std::vector<int> dsize = {};

    /**
     * @deprecated use gravity
     */
    bool padding = false;

    /**
     * 0: resize to fit new size
     * 1: resize to fit new size and keep aspect ratio
     * 2: resize to fit new size and algin top-left corner, make sure no empty space in vertival
     */
    int gravity            = 0;
    std::vector<int> color = {114, 114, 114};
};

class PUBLIC Normalize : public Op {
public:
    explicit Normalize(const std::string name_ = "normalize");
    virtual ~Normalize();

    virtual std::string Description() override;

    std::vector<float> mean = {0.0f, 0.0f, 0.0f};
    std::vector<float> std{};
    float scale = 1.0f;
    bool is_bgr = true;
};

class PUBLIC RectCrop : public Op {
public:
    explicit RectCrop(const std::string name_ = "rect_crop");

    virtual ~RectCrop();

    virtual std::string Description() override;

    std::string type                        = {};
    std::vector<float> bbox_hw_ratio_levels = {};
    std::vector<float> h_top_crop           = {};
    std::vector<float> h_bottom_crop        = {};
    std::vector<float> w_left_crop          = {};
    std::vector<float> w_right_crop         = {};

    // square
    bool square = false;

    /**
     * 0: max
     * 1: min
     * 2: avg
     */
    int square_mode = 0;
};

class PUBLIC CropResize : public Op {
public:
    explicit CropResize(const std::string name_ = "crop_resize");

    virtual ~CropResize();

    virtual std::string Description() override;

    std::string type                        = {};
    std::vector<float> bbox_hw_ratio_levels = {};
    std::vector<float> h_top_crop           = {};
    std::vector<float> h_bottom_crop        = {};
    std::vector<float> w_left_crop          = {};
    std::vector<float> w_right_crop         = {};
    bool square                             = false;
    int square_mode                         = 0;

    std::vector<int> dsize = {};
    int gravity            = 0;
    std::vector<int> color = {};
};

class PUBLIC YoloPost : public Op {
public:
    explicit YoloPost(const std::string name_ = "yolo_postprocess");

    virtual ~YoloPost();

    virtual std::string Description() override;

    float nms_threshold      = 0.35f;
    float nms_detection_conf = 0.1f;
    int top_k                = 1000;

    // Net input dimensions for coordinate denormalization.
    // When a model outputs normalized coordinates (0~1), the decode node
    // multiplies by these values to convert to pixel coordinates.
    int input_width  = 0;
    int input_height = 0;
};

class PUBLIC YoloNpuPost : public Op {
public:
    explicit YoloNpuPost(const std::string name_ = "yolo_npu_postprocess");

    virtual ~YoloNpuPost();

    virtual std::string Description() override;

    float nms_threshold      = 0.35f;
    float nms_detection_conf = 0.1f;
    int top_k                = 1000;

    std::vector<std::vector<std::vector<float>>> anchors{};
    std::vector<float> stride{};
};

class PUBLIC AffineCrop : public Op {
public:
    explicit AffineCrop(const std::string name_ = "affine_crop");

    virtual ~AffineCrop();

    virtual std::string Description() override;

    float norm_ratio;
    int norm_mode;
    std::vector<int> output_hw    = {};
    std::vector<int> center_index = {};
};

class PUBLIC Sum : public Op {
public:
    explicit Sum(const std::string name_ = "sum");

    virtual ~Sum();

    virtual std::string Description() override;
};

class PUBLIC Sequence : public Op {
public:
    explicit Sequence(const std::string name_ = "sequence");

    virtual ~Sequence();

    virtual std::string Description() override;

    int size;
    float scale;
    std::vector<int> dsize = {};
    bool is_bgr;
};

class PUBLIC ArgMax : public Op {
public:
    explicit ArgMax(const std::string name_ = "arg_max");

    virtual ~ArgMax();

    virtual std::string Description() override;

    int axis = 0;
};

class PUBLIC Split : public Op {
public:
    explicit Split(const std::string name_ = "split");

    virtual ~Split();

    virtual std::string Description() override;

    int axis = 0;
    std::vector<int> split{};
};

class PUBLIC SplitArgMax : public Op {
public:
    explicit SplitArgMax(const std::string name_ = "split_arg_max");

    virtual ~SplitArgMax();

    virtual std::string Description() override;

    std::vector<int> split{};
};

class PUBLIC CombineImage : public Op {
public:
    explicit CombineImage(const std::string name_ = "combine_image");

    virtual ~CombineImage();

    virtual std::string Description() override;

    int count      = 0;
    int dst_width  = 0;
    int dst_height = 0;
};

class DinoEncoder : public Op {
public:
    explicit DinoEncoder(const std::string name_ = "dino_encode");

    virtual ~DinoEncoder();

    virtual std::string Description() override;

    int dst_width;
    int dst_height;
    bool is_bgr;
    std::vector<float> mean;
    std::vector<float> std;
};

class DinoDecode : public Op {
public:
    explicit DinoDecode(const std::string name_ = "dino_decode");

    virtual ~DinoDecode();

    virtual std::string Description() override;

    float text_threshold;
    float box_threshold;
};

class SAMEncoder : public Op {
public:
    explicit SAMEncoder(const std::string name_ = "sam_encode");

    virtual ~SAMEncoder();

    virtual std::string Description() override;

    int dst_width;
    int dst_height;
    bool is_bgr;
    std::vector<float> mean;
    std::vector<float> std;
};

class SAMPromptEncode : public Op {
public:
    explicit SAMPromptEncode(const std::string name_ = "sam_prompt_encode");

    virtual ~SAMPromptEncode();

    virtual std::string Description() override;

    std::string prompt_type;  // "point" or "box"
    bool normalize;
    int encoder_size;  // encoder input size (e.g., 1024)
    int max_points;    // maximum number of points for padding
};

class SAMDecode : public Op {
public:
    explicit SAMDecode(const std::string name_ = "sam_decode");

    virtual ~SAMDecode();

    virtual std::string Description() override;

    float threshold;
    std::vector<int> output_size;  // [height, width] for mask resizing
};

}  // namespace cosmo::nn
