#include "nn/node/node.h"

#include "nn/utils/string_format.h"
namespace cosmo::nn {

Node::Node() {
    one_blob_only        = false;
    first_calculate_node = false;
    node_type            = NodeType::NODE_UNKNOWN;
    max_batch            = 1;
}

Node::~Node() {}

void Node::SetExtraResource(void* resource) {
    extra_resource = resource;
}

void Node::SetNodeName(std::string name) {
    this->name = name;
}

std::string Node::GetNodeName() {
    return name;
}

void Node::SetNodeType(NodeType type) {
    node_type = type;
}

void Node::UpdateNodeName(int count) {
    if (name.empty())
        return;

    auto strs = Spilt<std::string>(name, "_");
    ASSERT(strs.size() == 2);

    if (std::to_string(count) != strs.back()) {
        strs.back() = std::to_string(count);
        name        = Join(strs, "_");
    }
}

NodeType Node::GetNodeType() {
    return node_type;
}

size_t Node::GetMaxBatch() {
    return max_batch;
}

void Node::SetMaxBatch(size_t batch) {
    max_batch = batch;
}

size_t Node::GetBottomCount() {
    return 0;
}

size_t Node::GetTopCount() {
    return 0;
}

void Node::LoadParam(Op* op) {}

Status Node::InferTopShapes() {
    return COSMO_NN_OK;
}

Status Node::InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) {
    return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "InferTopShapesWithBottoms not implemented");
}

bool Node::NeedBottomShapesInfered() {
    if (first_calculate_node)
        return false;

    return false;
}

void Node::AddTopBlobName(std::string name) {
    top_blob_names.push_back(name);
}

std::vector<std::string> Node::GetTopBlobNames() {
    return top_blob_names;
}

void Node::SetTopBlobNames(std::vector<std::string> names) {
    top_blob_names = names;
}

std::vector<std::string> Node::GetBottomBlobNames() {
    return bottom_blob_names;
}

void Node::SetBottomBlobNames(std::vector<std::string> names) {
    bottom_blob_names = names;
}

std::vector<DimsVector> Node::GetTopBlobShapes() {
    return top_blob_shapes;
}

std::vector<DataType> Node::GetTopBlobDataTypes() {
    return top_blob_data_types;
}

DeviceType Node::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

double Node::GetInferTime() {
    return timer.GetTime();
}

void Node::SetSharedResource(SharedResource* resource) {
    shared_resource = resource;
}

void Node::SetCurrentBatch(std::vector<std::shared_ptr<Blob>>& tops, size_t batch) {
    for (auto& top : tops) {
        SetCurrentBatch(top, batch);
    }
}

void Node::SetCurrentBatch(std::shared_ptr<Blob> top, size_t batch) {
    auto desc       = top->GetBlobDesc();
    desc.dims.at(0) = batch;
    top->SetBlobDesc(desc);
}

Status Node::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    return Status(COSMO_NN_ERR_NODE_FORWARD, "Forward not implemented");
}

Status Node::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                     std::vector<std::shared_ptr<Blob>>& param,
                     std::vector<std::shared_ptr<Blob>>& top_blob) {
    return Status(COSMO_NN_ERR_NODE_FORWARD, "Forward not implemented");
}

Status CheckNodeForwardParam(Blob& bottom_blob, Blob& top_blob, bool check_same_device) {
    if (!bottom_blob.GetHandle().base)
        return Status(COSMO_NN_ERR_NULL_PARAM, "bottom blob is null");

    if (check_same_device && (bottom_blob.GetBlobDesc().device_type != top_blob.GetBlobDesc().device_type))
        return Status(COSMO_NN_ERR_NULL_PARAM, "bottom and top blob must be same device type");

    return COSMO_NN_OK;
}

Status CheckNodeInputOutput(std::shared_ptr<Blob>& bottom_blob, std::shared_ptr<Blob>& top_blob,
                            bool check_same_device) {
    if (!bottom_blob || !top_blob)
        return Status(COSMO_NN_ERR_NULL_PARAM, "bottom or top blob is null");

    if (!bottom_blob->GetHandle().base)
        return Status(COSMO_NN_ERR_NULL_PARAM, "bottom blob is null");

    if (!top_blob->GetHandle().base)
        return Status(COSMO_NN_ERR_NULL_PARAM, "top blob is null");

    if (check_same_device && (bottom_blob->GetBlobDesc().device_type != top_blob->GetBlobDesc().device_type))
        return Status(COSMO_NN_ERR_NULL_PARAM, "bottom and top blob must be same device type");

    return COSMO_NN_OK;
}

Status CheckNodeInputOutput(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                            std::vector<std::shared_ptr<Blob>>& top_blobs, bool check_same_device) {
    if (bottom_blobs.empty())
        return Status(COSMO_NN_ERR_NULL_PARAM, "bottom blobs is empty");

    if (top_blobs.empty())
        return Status(COSMO_NN_ERR_NULL_PARAM, "top blobs is empty");

    for (auto& blob : bottom_blobs) {
        if (!blob || !blob->GetHandle().base)
            return Status(COSMO_NN_ERR_NULL_PARAM, "bottom blob is null");
    }

    for (auto& blob : top_blobs) {
        if (!blob || !blob->GetHandle().base)
            return Status(COSMO_NN_ERR_NULL_PARAM, "top blob is null");
    }

    if (check_same_device) {
        for (size_t i = 0; i < bottom_blobs.size(); i++) {
            for (size_t j = 0; j < top_blobs.size(); j++) {
                if (bottom_blobs.at(i)->GetBlobDesc().device_type !=
                    top_blobs.at(j)->GetBlobDesc().device_type)
                    return Status(COSMO_NN_ERR_NULL_PARAM, "bottom and top blob must be same device type");
            }
        }
    }

    return COSMO_NN_OK;
}

}  // namespace cosmo::nn