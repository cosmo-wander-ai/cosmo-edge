#include "nn/node/node_creator.h"

namespace cosmo::nn {

NodeCreator::NodeCreator(DeviceType device_type_) : device_type(device_type_) {}

std::unique_ptr<Node> NodeCreator::CreateNode(NodeType type) {
    return nullptr;
}

std::map<DeviceType, std::shared_ptr<NodeCreator>>& GetGlobNodeCreatorMap() {
    static std::map<DeviceType, std::shared_ptr<NodeCreator>> node_creator_map;
    return node_creator_map;
}

NodeCreator* GetNodeCreator(DeviceType type) {
    return GetGlobNodeCreatorMap()[type].get();
}

}  // namespace cosmo::nn
