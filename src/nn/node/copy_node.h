#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief Abstract base class for data copy nodes between devices.
 *
 * Direction semantics:
 *   0 = HOST (CPU memory)
 *   1 = DEVICE (e.g. Sophon TPU device memory)
 */
class CopyNode : public Node {
public:
    CopyNode() {
        node_type     = NodeType::NODE_COPY;
        one_blob_only = true;
    }

    virtual ~CopyNode() = default;

    virtual void SetDirection(int from, int to) = 0;
    virtual int GetFrom()                       = 0;
    virtual int GetTo()                         = 0;

    virtual bool NeedBottomShapesInfered() override {
        return true;
    }
    virtual size_t GetBottomCount() override {
        return 1;
    }
    virtual size_t GetTopCount() override {
        return 1;
    }
};

}  // namespace cosmo::nn
