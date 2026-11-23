#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "nn/device/axera/axera_node_creator.h"

#include "nn/device/axera/axera_net_node.h"
#include "nn/device/host/host_node_factory.h"

namespace cosmo::nn {
AxeraNodeCreator::AxeraNodeCreator(DeviceType device_type) : NodeCreator(device_type) {}

std::unique_ptr<Node> AxeraNodeCreator::CreateNode(NodeType type) {
    if (type == NODE_NET)
        return std::make_unique<AxeraNetNode>();
    return CreateHostNode(type);
}

NodeCreatorRegister<AxeraNodeCreator> g_axera_node_creator_register(DEVICE_AXERA);
}  // namespace cosmo::nn
#endif
