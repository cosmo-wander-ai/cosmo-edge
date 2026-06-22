#pragma once

#include "nn/node/node.h"
#include "nn/node/node_type.h"

namespace cosmo::nn {

class NodeTypeUtils {
public:
    static NodeType NodeTypeFromStr(std::string name);
    static std::string NodeTypeToStr(NodeType type);

    static bool IsInputNode(Node* node);

    static std::unique_ptr<Node> CreateByType(NodeType type, DeviceType device = DEVICE_SOPHON_TPU);

    static std::unique_ptr<Node> CreateNode(NodeType type, int count, int max_batch,
                                            DeviceType device = DEVICE_SOPHON_TPU);

    static int TypedNodeCount(const std::vector<std::unique_ptr<Node>>& nodes, NodeType type);
};

}  // namespace cosmo::nn
