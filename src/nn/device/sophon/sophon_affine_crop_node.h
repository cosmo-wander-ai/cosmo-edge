#pragma once

#include "nn/node/node.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn {

/**
 * @brief SophonAffineCropNode
 * AffineCropNode on sophon device.
 */
class SophonAffineCropNode : public Node {
public:
    SophonAffineCropNode();

    virtual ~SophonAffineCropNode();

    virtual void LoadParam(Op* op) override;

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual Status InferTopShapes() override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    /**
     * @brief Forward
     *
     * @param bottom_blob. bottom blob. [image_blob]
     * @param params. params blob list. [landmark_blob]
     * @param top_blobs. top blobs list. [crop_blob]
     * @return Status
     */
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                           std::vector<std::shared_ptr<Blob>>& params,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    /**
     * @brief Forward
     *
     * @param bottom_blobs. bottom blobs list. [image_blob, landmark_blob]
     * @param top_blobs. top blobs list. [crop_blob]
     * @return Status
     */
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                   std::vector<std::shared_ptr<Blob>>& landmark_blobs, std::shared_ptr<Blob> top_blob);

    Status Forward(std::shared_ptr<Blob> image_blob, std::shared_ptr<Blob> landmark_blob,
                   std::shared_ptr<Blob> top_blob);

    Status PrepareAffineCropMatrixs(std::shared_ptr<Blob> landmark_blob);

    Status CalculateMatrix(float* landmark, std::vector<float>& matrix);

private:
    float norm_ratio;
    int norm_mode;
    int dst_width                 = 0;
    int dst_height                = 0;
    std::vector<int> center_index = {};

    const int landmark_data_len = 10;

    const size_t matrix_len = 6;
    std::vector<float> host_matrixs;
};

}  // namespace cosmo::nn
