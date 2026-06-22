#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/copy_node.h"

namespace cosmo::nn {

/**
 * @brief CPU copy node — pure CPU memcpy.
 *
 * In CPU-only mode all data lives in host memory, so this node
 * simply performs memcpy (or acts as a no-op when src == dst).
 */
class CpuCopyNode : public CopyNode {
public:
    CpuCopyNode();
    ~CpuCopyNode() override = default;

    void SetDirection(int from, int to) override;
    int GetFrom() override;
    int GetTo() override;

    DeviceType GetTopBlobDeviceType() override;
    void LoadParam(Op* op) override;

    Status InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    int from_ = 0;
    int to_   = 0;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
