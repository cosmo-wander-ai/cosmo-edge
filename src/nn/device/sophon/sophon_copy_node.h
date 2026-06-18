#pragma once

#include "nn/node/copy_node.h"

namespace cosmo::nn {

/**
 * @brief SophonCopyNode
 *
 * SophonCopyNode is a node that copy data from one device to another device.
 * Default direction is Sophon DEVICE -> HOST
 */
class SophonCopyNode : public CopyNode {
public:
    SophonCopyNode();

    ~SophonCopyNode();

    virtual Status InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                             std::vector<DataType> types) override;

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual void LoadParam(Op* op) override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    void SetDirection(int from, int to) override;
    int GetFrom() override;
    int GetTo() override;

private:
    /**
     * @brief from device type
     * 0 : HOST
     * 1 : SOPHON DEVICE
     */
    int from = 1;
    int to   = 0;
};

}  // namespace cosmo::nn
