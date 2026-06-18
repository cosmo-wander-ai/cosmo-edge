#include "nn/node/input_node.h"

#include "nn/node/node_type_utils.h"

namespace cosmo::nn {

InputNode::InputNode() : Node() {
    node_type     = NodeType::NODE_INPUT;
    name          = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_INPUT).append("_0");
    one_blob_only = true;
}

InputNode::~InputNode() {}

void InputNode::LoadParam(Op* op) {}

Status InputNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                          std::vector<std::shared_ptr<Blob>>& params,
                          std::vector<std::shared_ptr<Blob>>& top_blobs) {
    return COSMO_NN_OK;
}

Status InputNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                          std::vector<std::shared_ptr<Blob>>& top_blobs) {
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn