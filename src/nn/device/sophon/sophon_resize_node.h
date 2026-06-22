#pragma once

#include <memory>

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief SophonResizeNode is a node to resize image(Bilinear)
 */
class SophonResizeNode : public Node {
public:
    SophonResizeNode();

    virtual ~SophonResizeNode();

    virtual void LoadParam(Op* op) override;

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual Status InferTopShapes() override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    /**
     * @brief resize image
     *
     * @param bottom_blobs bottom blobs
     * @param top_blobs top blobs
     * @return Status
     *
     * @note while @memberof Node::first_calculate_node is true,
     *       bottom_blobs will be [image0, image1, ...],
     *       otherwise bottom_blobs will be [image]
     */
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status Forward(std::shared_ptr<Blob>& bottom_blob, std::shared_ptr<Blob>& top_blob);

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs, std::shared_ptr<Blob>& top_blob);

private:
    int out_h    = 0;
    int out_w    = 0;
    int gravity  = 0;
    u_char color = 114;
};

}  // namespace cosmo::nn
