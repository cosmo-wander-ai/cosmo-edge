#include "nn/node/concat_node.h"

#include <string.h>

#include <algorithm>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"

namespace cosmo::nn {

ConcatNode::ConcatNode() : Node() {
    node_type     = NodeType::NODE_CONCAT;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_CONCAT).append("_0");
    one_blob_only = false;
}

ConcatNode::~ConcatNode() {}

size_t ConcatNode::GetBottomCount() {
    return bottom_blob_names.size();
}

size_t ConcatNode::GetTopCount() {
    return 1;
}

bool ConcatNode::NeedBottomShapesInfered() {
    return true;
}

Status ConcatNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) {
    // check bottom dims/data types
    auto dims_count = dims.size();
    auto type_count = types.size();
    if (dims_count != type_count)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "dims count != type count");

    const int n_dim     = dims.at(0).size();
    const DataType type = types.at(0);

    for (int i = 1; i < dims_count; ++i) {
        if (dims.at(i).size() != n_dim)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "dims size != n_dim");

        if (types.at(i) != type)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "dims data type != type");
    }

    if (axis < 0)
        axis += n_dim;

    if (axis >= n_dim)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "axis >= n_dim");

    top_blob_data_types = {type};

    DimsVector top_dims = dims.at(0);
    top_dims.at(axis)   = 0;
    for_each(dims.begin(), dims.end(), [&](DimsVector& dims) { top_dims.at(axis) += dims.at(axis); });

    top_blob_shapes = {top_dims};

    return COSMO_NN_OK;
}

Status ConcatNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));

    int batch         = 0;
    auto bottom_count = bottom_blobs.size();
    for (int i = 0; i < bottom_count; ++i) {
        if (batch == 0) {
            batch = bottom_blobs.at(i)->GetBlobDesc().dims.at(0);
        } else {
            if (batch != bottom_blobs.at(i)->GetBlobDesc().dims.at(0))
                return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size not same");
        }

        if (batch > max_batch)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");
    }

    auto top_blob = top_blobs.at(0);

    SetCurrentBatch(top_blob, batch);

    auto top_desc   = top_blob->GetBlobDesc();
    auto top_dims   = top_desc.dims;
    auto top_handle = top_blob->GetHandle();

    auto data_type_bytes = DataTypeUtils::GetBytesSize(top_desc.data_type);

    const int n_dim = top_dims.size();
    if (n_dim == 2) {
        if (axis != 1)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid axis");

        const int top_width = top_dims.at(1);
        int offset          = 0;
        for (int i = 0; i < bottom_count; ++i) {
            auto bottom_blob   = bottom_blobs.at(i);
            auto bottom_dims   = bottom_blob->GetBlobDesc().dims;
            auto bottom_handle = bottom_blob->GetHandle();

            const int bottom_height = bottom_dims.at(0);
            const int bottom_width  = bottom_dims.at(1);

            for (int j = 0; j < bottom_height; ++j) {
                void* top_row    = GET_OFFSET_PTR(top_handle.base, j * top_width * data_type_bytes);
                void* bottom_row = GET_OFFSET_PTR(bottom_handle.base, j * bottom_width * data_type_bytes);
                memcpy(GET_OFFSET_PTR(top_row, offset), bottom_row, bottom_width * data_type_bytes);
            }

            offset += bottom_width * data_type_bytes;
        }

    } else if (n_dim == 3) {
        return Status(COSMO_NN_ERR_NOT_IMPLEMENTED, "concat 3D not implemented");
    } else {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "concat node only support 2D or 3D");
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn