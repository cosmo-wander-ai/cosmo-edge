#include "nn/node/node.h"

namespace cosmo::nn {

class NodeCreator {
public:
    explicit NodeCreator(DeviceType);

    virtual ~NodeCreator() {}

    virtual std::unique_ptr<Node> CreateNode(NodeType type);

protected:
    DeviceType device_type;
};

std::map<DeviceType, std::shared_ptr<NodeCreator>>& GetGlobNodeCreatorMap();

NodeCreator* GetNodeCreator(DeviceType);

template <typename T>
class NodeCreatorRegister {
public:
    explicit NodeCreatorRegister(DeviceType type) {
        auto& map = GetGlobNodeCreatorMap();
        if (map.find(type) == map.end()) {
            map[type] = std::shared_ptr<T>(new T(type));
        }
    }
};

}  // namespace cosmo::nn