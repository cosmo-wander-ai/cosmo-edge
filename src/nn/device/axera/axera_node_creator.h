#pragma once

#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/node/node_creator.h"

namespace cosmo::nn {
class AxeraNodeCreator final : public NodeCreator {
public:
    explicit AxeraNodeCreator(DeviceType device_type);
    std::unique_ptr<Node> CreateNode(NodeType type) override;
};
}  // namespace cosmo::nn
#endif
