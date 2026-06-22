#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/node_creator.h"

namespace cosmo::nn {

/**
 * @brief CPU device node creator.
 *
 * Creates CPU-specific nodes for resize, normalize, net and copy operations.
 * Registered as DEVICE_CPU in the global node creator map.
 */
class CpuNodeCreator : public NodeCreator {
public:
    explicit CpuNodeCreator(DeviceType device_type);
    ~CpuNodeCreator() override = default;

    std::unique_ptr<Node> CreateNode(NodeType type) override;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
