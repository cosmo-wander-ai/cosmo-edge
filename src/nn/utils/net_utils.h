#pragma once

#include <cmath>
#include <memory>
#include <optional>

#include "nn/core/blob.h"
#include "nn/core/macros.h"
#include "nn/core/status.h"
#include "nn/utils/model_info_utils.h"
#include "nn/utils/rect.h"
#include "util/DetectionGeometry.h"

namespace cosmo::nn {

template <typename T1, typename T2, typename T3>
using triple = std::tuple<T1, T2, T3>;

struct PUBLIC ObjectInfo {
    float x1 = 0;
    float y1 = 0;
    float x2 = 0;
    float y2 = 0;

    // key_points <x y>
    std::vector<std::pair<float, float>> key_points = {};
    // ket_points_3d <x y z>
    std::vector<triple<float, float, float>> key_points_3d = {};
    // key_points confidence
    std::vector<float> key_point_confidences = {};

    float confidence       = 0;
    int class_id           = -1;
    std::string class_name = {};
    float angle            = 0;
};

struct PUBLIC ClassifyInfo {
    float confidence       = 0;
    int class_id           = -1;
    std::string class_name = {};
};

struct PUBLIC ObjectInfoV1 {
    float x1 = 0;
    float y1 = 0;
    float x2 = 0;
    float y2 = 0;

    // key_points <x y>
    std::vector<std::pair<float, float>> key_points = {};
    // ket_points_3d <x y z>
    std::vector<triple<float, float, float>> key_points_3d = {};
    // key_points confidence
    std::vector<float> key_point_confidences = {};

    float angle                     = 0;
    std::vector<ClassifyInfo> infos = {};

    // x1/y1/x2/y2 always describe the axis-aligned bounds. Oriented geometry
    // is optional, including for an oriented box with zero rotation.
    std::optional<util::Quad> oriented_corners;
};

typedef enum { NONE = 0, SSD = 1, YOLO = 2 } OutputDecodeType;

typedef enum { KEY_POINT_2D = 0, KEY_POINT_3D = 1 } KeyPointType;

struct PUBLIC OutputCalculateParam {
    OutputDecodeType type                   = YOLO;
    std::vector<float> selected_thresholds  = {};
    std::vector<std::string> selected_label = {};
    std::vector<int> selected_index         = {};

    float iou_threshold = 0.65f;
    int current_batch   = 1;

    int key_point_num           = 0;
    KeyPointType key_point_type = KEY_POINT_2D;

    bool has_degree = false;
};

struct PUBLIC Size {
    Size() = default;
    explicit Size(int w);
    Size(int w, int h);
    int width  = 0;
    int height = 0;
};

struct PUBLIC CropResizeParam {
    // roi
    int top_left_x = 0;
    int top_left_y = 0;
    int width      = 0;
    int height     = 0;

    // resize
    int gravity    = 0;
    int dst_width  = 0;
    int dst_height = 0;
    uint8_t val    = 114;
};

class PUBLIC NetUtils {
public:
    static float Clamp(float x, float low, float high);

    static float Exp(float x, int terms = 10);

    static float Sigmoid(float x);

    static void SwapYUV_I420toYUV_NV12(const unsigned char* i420bytes, unsigned char* nv12bytes, int width,
                                       int height);
    // static std::shared_ptr<Net> CreateNet(ModelConfig model_cfg_, BackendConfig backend_cfg_,
    //                                       ShapesMap max_input_shapes_, Status& status);

    static void NMS(std::vector<ObjectInfo>& input, std::vector<ObjectInfo>& output, float iou_threshold_);

    static Status PickDetectionObjects(std::shared_ptr<Blob>& blob, const std::vector<Size>& input_sizes,
                                       Size net_input_size, std::vector<int> selected_indcies,
                                       const std::vector<float>& selected_thresholds,
                                       const std::vector<std::string>& selected_label,
                                       std::vector<std::vector<ObjectInfoV1>>& detects);

    static Status GetImageSize(const DimsVector&, DataFormat, Size&);

    /**
     * gravity
     *  0   : reisze
     *  1   : resize to fit the new size and keep aspect, empty apace may be remain zero
     *  2   : resize to fit the new size and keep aspect, no empty space remain
     */
    static ObjectInfo AdjustSize(const ObjectInfo& info, Size old_, Size new_, int gravity = 0);

    static ObjectInfoV1 AdjustSize(const ObjectInfoV1& info, Size old_, Size new_, int gravity = 0);

    static Status GenerateCropResizeParam(CropResize* crop_resize, int left, int top, int right, int bottom,
                                          Size size, CropResizeParam& param);

    static Status ParseDetectionOutput(const std::vector<std::shared_ptr<Blob>>& blobs,
                                       const std::vector<Size>& image_sizes, Size net_input_size,
                                       const std::vector<int>& selected_indcies,
                                       const std::vector<float>& selected_thresholds,
                                       const std::vector<std::string>& selected_label,
                                       std::vector<std::vector<ObjectInfoV1>>& detects);

    // Input is host float [B,N,6+K]: center xywh, person score, class 0 and
    // K scores in attribute_names order. Empty names select a known legacy layout.
    static Status ParsePersonAttributesOutput(const std::vector<std::shared_ptr<Blob>>& blobs,
                                              const std::vector<Size>& image_sizes, Size net_input_size,
                                              float person_threshold, const std::string& person_label,
                                              std::vector<std::vector<ObjectInfoV1>>& detects,
                                              const std::vector<std::string>& attribute_names = {});

    static Status ParseClassificationOutput(const std::vector<std::shared_ptr<Blob>>& blobs,
                                            std::vector<int>& selected_indcies,
                                            const std::vector<std::string>& selected_label,
                                            std::vector<std::vector<ObjectInfoV1>>& detects);

    static Status ParseKeypointOutput(const std::vector<std::shared_ptr<Blob>>& blobs,
                                      const std::vector<Rect>& rects,
                                      std::vector<std::vector<ObjectInfoV1>>& detects);

    static std::string TokenizerDecode(void* handle, std::vector<int>& ids);

    static Status ParseDINOOutput(const std::vector<std::shared_ptr<Blob>>& blobs, void* tokenizer_handle,
                                  const std::vector<Size>& input_sizes,
                                  const std::vector<int>& padding_input_ids, float text_threshold,
                                  float box_threshold, std::vector<std::vector<ObjectInfoV1>>& detects);

    static void CalculateWarpAffineMatrix(const std::vector<std::pair<float, float>>& src_points,
                                          const std::vector<std::pair<float, float>>& dst_points,
                                          std::vector<float>& matrix);

    static void WarpAffineMatrixInverse(const std::vector<float>& matrix, std::vector<float>& inverse);

    // High-level warp affine: compute matrix from src/dst points, then apply transform on device
    static Status WarpAffine(std::shared_ptr<Blob> blob,
                             const std::vector<std::pair<float, float>>& src_points,
                             std::shared_ptr<Blob>& out_blob, float x_scale = 0.03f, float y_scale = 0.02f);

    static void MaskScale(uint8_t* src, size_t src_width, size_t src_height, uint8_t* dst, size_t dst_width,
                          size_t dst_height, int channel);
};

}  // namespace cosmo::nn
