#pragma once

#include "nn/node/node.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn {

// Actual preprocessing affine transform: input = source * scale + offset.
// The pipeline selects this from its configured resize operation and backend.
struct ObbResizeTransform {
    float scale_x  = 1.f;
    float scale_y  = 1.f;
    float offset_x = 0.f;
    float offset_y = 0.f;
};

// This parser accepts the explicit corner output of YoloObbDecodeNode only.
Status ParseYoloObbOutput(const std::shared_ptr<Blob>& blob,
                          const std::vector<ObbResizeTransform>& transforms,
                          const std::vector<int>& selected_indices,
                          const std::vector<float>& selected_thresholds,
                          const std::vector<std::string>& selected_labels,
                          std::vector<std::vector<ObjectInfoV1>>& outputs, float default_threshold = 0.25f);

/**
 * End-to-end YOLO OBB adaptation (NMS is already performed by the network).
 * Input: float32 [batch, rows, 7]: cx, cy, w, h, score, class_id, radians.
 * Coordinates are input-image pixels unless explicitly configured normalized.
 * Output: float32 [batch, top_k, 10]: four consecutive (x,y) vertices followed
 * by score and class_id, in input-image pixel coordinates.
 */
class YoloObbDecodeNode : public Node {
public:
    YoloObbDecodeNode();
    ~YoloObbDecodeNode() override;

    void LoadParam(Op* op) override;
    Status InferTopShapes() override;
    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;

private:
    float base_conf                     = 0.25f;
    int top_k                           = 300;
    static constexpr int kOutputColumns = 10;
    int input_width_                    = 0;
    int input_height_                   = 0;
    bool normalized_coordinates_        = false;
    bool valid_params_                  = false;
};

}  // namespace cosmo::nn
