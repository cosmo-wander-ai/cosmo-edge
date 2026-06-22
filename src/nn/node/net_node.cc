#include "nn/node/net_node.h"

#include "nn/node/node_type_utils.h"

namespace cosmo::nn {

NetNode::NetNode() : Node() {
    node_type = NodeType::NODE_NET;
    name      = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_NET).append("_0");
}

NetNode::~NetNode() {}

void NetNode::LoadParam(Op* op) {}

size_t NetNode::GetBottomCount() {
    return bottom_count;
}

size_t NetNode::GetTopCount() {
    return top_count;
}

void NetNode::SetNetworkInputNames(std::vector<std::string> names) {
    network_input_names = names;
    bottom_count        = names.size();
}

void NetNode::SetNetworkOutputNames(std::vector<std::string> names) {
    network_output_names = names;
    top_count            = names.size();
}

Status NetNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                        std::vector<std::shared_ptr<Blob>>& params,
                        std::vector<std::shared_ptr<Blob>>& top_blobs) {
    return COSMO_NN_OK;
}

Status NetNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                        std::vector<std::shared_ptr<Blob>>& top_blobs) {
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn