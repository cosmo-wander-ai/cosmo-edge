#include "nn/node/node_creator.h"

#include <mutex>

namespace cosmo::nn {

NodeCreator::NodeCreator(DeviceType device_type_) : device_type(device_type_) {}

std::unique_ptr<Node> NodeCreator::CreateNode(NodeType type) {
    return nullptr;
}

std::map<DeviceType, std::shared_ptr<NodeCreator>>& GetGlobNodeCreatorMap() {
    static std::once_flag node_creator_once;
    static std::shared_ptr<std::map<DeviceType, std::shared_ptr<NodeCreator>>> node_creator_map;

    std::call_once(node_creator_once, []() {
        node_creator_map.reset(new std::map<DeviceType, std::shared_ptr<NodeCreator>>());
    });

    return *node_creator_map;
}

NodeCreator* GetNodeCreator(DeviceType type) {
    return GetGlobNodeCreatorMap()[type].get();
}

}  // namespace cosmo::nn