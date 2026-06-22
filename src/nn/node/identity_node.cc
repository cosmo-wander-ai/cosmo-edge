#include "nn/node/identity_node.h"

#include <cstring>
#include <iostream>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"

namespace cosmo::nn {

IdentityNode::IdentityNode() : Node() {
    node_type          = NodeType::NODE_IDENTITY;
    name               = NodeTypeUtils::NodeTypeToStr(NODE_IDENTITY).append("_0");
    one_blob_only      = true;
    expected_data_type = DATA_TYPE_FLOAT;  // Default to float
}

IdentityNode::~IdentityNode() {}

void IdentityNode::LoadParam(Op* op) {
    // IdentityNode doesn't need any parameters from Op
    // Shape and data type are set directly via SetExpectedShape
}

size_t IdentityNode::GetTopCount() {
    return 1;
}

size_t IdentityNode::GetBottomCount() {
    return 1;
}

void IdentityNode::SetExpectedShape(const DimsVector& shape, DataType data_type) {
    expected_shape     = shape;
    expected_data_type = data_type;
}

Status IdentityNode::InferTopShapes() {
    // For first_calculate_node, use expected shape if set
    if (first_calculate_node && !expected_shape.empty()) {
        top_blob_shapes     = {expected_shape};
        top_blob_data_types = {expected_data_type};
        return COSMO_NN_OK;
    }

    // If not a first_calculate_node, this should not be called
    // (NeedBottomShapesInfered should return true)
    return Status(COSMO_NN_ERR_PARAM, "IdentityNode::InferTopShapes called but expected_shape not set");
}

bool IdentityNode::NeedBottomShapesInfered() {
    // If this is a first_calculate_node, we don't have bottom blobs yet
    if (first_calculate_node) {
        return false;
    }
    return true;
}

Status IdentityNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) {
    if (dims.empty()) {
        return Status(COSMO_NN_ERR_PARAM, "identity node needs at least 1 bottom blob");
    }

    // Output shape is same as input shape
    top_blob_data_types = {types[0]};
    top_blob_shapes     = {dims[0]};

    return COSMO_NN_OK;
}

Status IdentityNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                             std::vector<std::shared_ptr<Blob>>& params,
                             std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    // For first_calculate_node, params contains the user input
    if (params.empty() || top_blobs.empty()) {
        return Status(COSMO_NN_ERR_PARAM, "IdentityNode: params or top_blobs is empty");
    }

    auto input_blob  = params.at(0);
    auto output_blob = top_blobs.at(0);

    // Don't check device type match - identity node can transfer between devices
    RETURN_ON_FAIL(CheckNodeInputOutput(input_blob, output_blob, false));

    auto input_desc    = input_blob->GetBlobDesc();
    auto input_handle  = input_blob->GetHandle();
    auto output_handle = output_blob->GetHandle();

    const int batch = input_desc.dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    SetCurrentBatch(output_blob, batch);

    // Calculate total size
    size_t element_count = 1;
    for (auto dim : input_desc.dims) {
        element_count *= dim;
    }
    size_t byte_size = element_count * DataTypeUtils::GetBytesSize(input_desc.data_type);

    // Simply copy input to output
    std::memcpy(output_handle.base, input_handle.base, byte_size);

    timer.Stop();
    return COSMO_NN_OK;
}

Status IdentityNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                             std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    // Don't check device type match - identity node can transfer between devices
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, false));

    auto input_blob  = bottom_blobs.at(0);
    auto output_blob = top_blobs.at(0);

    auto input_desc    = input_blob->GetBlobDesc();
    auto input_handle  = input_blob->GetHandle();
    auto output_handle = output_blob->GetHandle();

    const int batch = input_desc.dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    SetCurrentBatch(output_blob, batch);

    // Calculate total size
    size_t element_count = 1;
    for (auto dim : input_desc.dims) {
        element_count *= dim;
    }
    size_t byte_size = element_count * DataTypeUtils::GetBytesSize(input_desc.data_type);

    // Simply copy input to output
    std::memcpy(output_handle.base, input_handle.base, byte_size);

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
