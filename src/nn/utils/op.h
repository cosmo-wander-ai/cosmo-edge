#pragma once

#include <string>
#include <vector>

#include "nn/core/macros.h"

namespace cosmo::nn {

class PUBLIC Op {
public:
    explicit Op(std::string name_);
    virtual ~Op();

    virtual std::string Description() = 0;

    std::string name = {};
};

class PUBLIC UnknownOp : public Op {
public:
    explicit UnknownOp(std::string name_ = "Unknown");
    virtual ~UnknownOp();

    virtual std::string Description() override;
};

class PUBLIC Resize : public Op {
public:
    explicit Resize(std::string name_ = "resize");
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
    explicit Normalize(std::string name_ = "normalize");
    virtual ~Normalize();

    virtual std::string Description() override;

    std::vector<float> mean = {0.0f, 0.0f, 0.0f};
    std::vector<float> std{};
    float scale = 1.0f;
    bool is_bgr = true;
};

class PUBLIC CropResize : public Op {
public:
    explicit CropResize(std::string name_ = "crop_resize");

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
    explicit YoloPost(std::string name_ = "yolo_postprocess");

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

    // YOLO E2E decoder only: opt in to channel-major [B, 4 + classes, N]
    // pixel xywh and class probabilities; the default is decoded [B, N, 6].
    bool raw_output = false;

    // Internal YOLO26 pipeline flag; never serialized into model params.
    bool auto_detect_output = false;

    // Decoded person-attribute row width, including the six detection columns.
    int person_attribute_columns = 10;

    // OBB model contract only. Ordinary YOLO decoders retain their own format.
    bool normalized_coordinates = false;
};

class PUBLIC YoloNpuPost : public Op {
public:
    explicit YoloNpuPost(std::string name_ = "yolo_npu_postprocess");

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
    explicit AffineCrop(std::string name_ = "affine_crop");

    virtual ~AffineCrop();

    virtual std::string Description() override;

    float norm_ratio              = 0.4f;
    int norm_mode                 = 1;
    std::vector<int> output_hw    = {};
    std::vector<int> center_index = {};
};

class PUBLIC Sum : public Op {
public:
    explicit Sum(std::string name_ = "sum");

    virtual ~Sum();

    virtual std::string Description() override;
};

class PUBLIC Sequence : public Op {
public:
    explicit Sequence(std::string name_ = "sequence");

    virtual ~Sequence();

    virtual std::string Description() override;

    int size               = 0;
    float scale            = 1.0f;
    std::vector<int> dsize = {};
    bool is_bgr            = true;
};

class PUBLIC ArgMax : public Op {
public:
    explicit ArgMax(std::string name_ = "arg_max");

    virtual ~ArgMax();

    virtual std::string Description() override;

    int axis = 0;
};

class PUBLIC Split : public Op {
public:
    explicit Split(std::string name_ = "split");

    virtual ~Split();

    virtual std::string Description() override;

    int axis = 0;
    std::vector<int> split{};
};

class PUBLIC SplitArgMax : public Op {
public:
    explicit SplitArgMax(std::string name_ = "split_arg_max");

    virtual ~SplitArgMax();

    virtual std::string Description() override;

    std::vector<int> split{};
};

class PUBLIC CombineImage : public Op {
public:
    explicit CombineImage(std::string name_ = "combine_image");

    virtual ~CombineImage();

    virtual std::string Description() override;

    int count      = 0;
    int dst_width  = 0;
    int dst_height = 0;
};

class DinoEncoder : public Op {
public:
    explicit DinoEncoder(std::string name_ = "dino_encode");

    virtual ~DinoEncoder();

    virtual std::string Description() override;

    int dst_width  = 0;
    int dst_height = 0;
    bool is_bgr    = false;
    std::vector<float> mean;
    std::vector<float> std;
};

class DinoDecode : public Op {
public:
    explicit DinoDecode(std::string name_ = "dino_decode");

    virtual ~DinoDecode();

    virtual std::string Description() override;

    float text_threshold = 0.25f;
    float box_threshold  = 0.3f;
};

class SAMEncoder : public Op {
public:
    explicit SAMEncoder(std::string name_ = "sam_encode");

    virtual ~SAMEncoder();

    virtual std::string Description() override;

    int dst_width  = 0;
    int dst_height = 0;
    bool is_bgr    = false;
    std::vector<float> mean;
    std::vector<float> std;
};

class SAMPromptEncode : public Op {
public:
    explicit SAMPromptEncode(std::string name_ = "sam_prompt_encode");

    virtual ~SAMPromptEncode();

    virtual std::string Description() override;

    std::string prompt_type;  // "point" or "box"
    bool normalize   = true;
    int encoder_size = 1024;  // encoder input size (e.g., 1024)
    int max_points   = 6;     // maximum number of points for padding
};

class SAMDecode : public Op {
public:
    explicit SAMDecode(std::string name_ = "sam_decode");

    virtual ~SAMDecode();

    virtual std::string Description() override;

    float threshold = 0.0f;
    std::vector<int> output_size;  // [height, width] for mask resizing
};

}  // namespace cosmo::nn
