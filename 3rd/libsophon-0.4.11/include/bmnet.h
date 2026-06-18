/*****************************************************************************
 *
 *    Copyright (c) 2016-2026 by Sophgo Technologies Inc. All rights reserved.
 *
 *    The material in this file is confidential and contains trade secrets
 *    of Sophgo Technologies Inc. This is proprietary information owned by
 *    Sophgo Technologies Inc. No part of this work may be disclosed,
 *    reproduced, copied, transmitted, or used in any way for any purpose,
 *    without the express written permission of Sophgo Technologies Inc.
 *
 *****************************************************************************/

#ifndef __BM_NET_H__
#define __BM_NET_H__

#include "bmblob.h"
#include "bmcnnctx.h"
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace bmcnn {

class BMNet
{
public:
    /**
     * \brief Constructor of net.
     *
     * \param handle - Handler of BMCNN context (created by \ref bmcnn_ctx_create)
     * \param name - Name of net
     */
    explicit BMNet(bmcnn_ctx_t handle, const std::string &name);
    /**
     * \brief Deconstructor of blob.
     */
    virtual ~BMNet();
    /**
     * \brief Reshape all layers from bottom to top.
     */
    void Reshape();
    /**
     * \brief Synchronize the net shape to device after forward
     *
     * \note
     * To get the accurate output shape of the network that can vary height and width,\n
     * we need to synchronze the shape after forwarding.\n
     */
    void SyncShape();
    /**
     * \brief Run forward.
     *
     * \param sync - Flag of synchronizing.
     */
    void Forward(bool sync = false);
    /**
     * \brief Get blob by name.
     *
     * \param name - Name of blob
     * \note
     * (1) The name could only be of blob in input or output.\n
     * (2) If the name is not spotted, null pointer will be returned.\n
     */
    const std::shared_ptr<BMBlob> blob_by_name(const std::string &name) const;
    /**
     * \brief Get maximum shape allowed.
     */
    inline const Shape &max_shape() const
    { return max_shape_; }
private:
    BMNet(const BMNet &other);
    BMNet &operator=(const BMNet &other);

    bmcnn_ctx_t bmcc_ctx_;
    std::vector<std::shared_ptr<BMBlob> > blobs_;
    std::vector<BMBlob *> net_input_blobs_;
    std::vector<BMBlob *> net_output_blobs_;
    std::map<std::string, size_t> blob_name_index_;
    Shape max_shape_;
    int net_idx_;
    const bm_net_info_t * net_info_;
};

} /* namespace bmcnn */

#endif /* __BM_NET_H__ */
