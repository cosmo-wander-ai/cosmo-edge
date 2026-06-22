#pragma once

#include "nn/node/node_creator.h"

namespace cosmo::nn {

class SophonNodeCreator : public NodeCreator {
public:
    explicit SophonNodeCreator(DeviceType);

    virtual ~SophonNodeCreator();

    virtual std::unique_ptr<Node> CreateNode(NodeType type) override;
};

}  // namespace cosmo::nn
