#pragma once

#include "nn/node/node_creator.h"

namespace cosmo::nn {

class HostNodeCreator : public NodeCreator {
public:
    explicit HostNodeCreator(DeviceType);

    virtual ~HostNodeCreator();

    virtual std::unique_ptr<Node> CreateNode(NodeType type) override;
};

}  // namespace cosmo::nn
