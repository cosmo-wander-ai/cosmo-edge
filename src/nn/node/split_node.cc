#include "nn/node/split_node.h"

#include <string.h>

#include <algorithm>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"

namespace cosmo::nn {

SplitNode::SplitNode() : Node() {
    node_type     = NodeType::NODE_SPLIT;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_SPLIT).append("_0");
    one_blob_only = false;
}

SplitNode::~SplitNode() {}

void SplitNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto* s = dynamic_cast<Split*>(op);
    axis    = s->axis;
    split   = s->split;
}

size_t SplitNode::GetBottomCount() {
    return 1;
}

size_t SplitNode::GetTopCount() {
    return split.size();
}

bool SplitNode::NeedBottomShapesInfered() {
    return true;
}

Status SplitNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) {
    if (dims.size() != 1 || types.size() != 1)
        return Status(COSMO_NN_ERR_PARAM, "SplitNode: Invalid bottom count");

    auto bottom_dims = dims.at(0);
    auto bottom_type = types.at(0);

    if (axis < 0)
        axis += bottom_dims.size();

    if (axis >= bottom_dims.size())
        return Status(COSMO_NN_ERR_PARAM, "SplitNode: Invalid axis");

    int len = 0;
    std::for_each(split.begin(), split.end(), [&](int s) { len += s; });

    if (len != bottom_dims.at(axis))
        return Status(COSMO_NN_ERR_PARAM, "SplitNode: Invalid split");

    top_blob_data_types.assign(split.size(), bottom_type);

    for (int i = 0; i < split.size(); i++) {
        DimsVector dims_i = bottom_dims;
        dims_i[axis]      = split[i];
        top_blob_shapes.push_back(dims_i);
    }

    return COSMO_NN_OK;
}

Status SplitNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                          std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));
    if (axis != 1)
        return Status(COSMO_NN_ERR_PARAM, "SplitNode: Only support axis=1");

    auto bottom_blob = bottom_blobs.at(0);
    auto bottom_desc = bottom_blob->GetBlobDesc();
    auto bottom_dims = bottom_desc.dims;

    const int batch = bottom_dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    SetCurrentBatch(top_blobs, batch);

    const int n_dim = bottom_dims.size();

    void* bottom_data    = bottom_blob->GetHandle().base;
    auto data_type_bytes = DataTypeUtils::GetBytesSize(bottom_desc.data_type);

    if (n_dim == 2) {
        const int bottom_width = bottom_dims.at(1);

        int passed_len = 0;
        for (int i = 0; i < top_blobs.size(); i++) {
            auto top_blob = top_blobs.at(i);
            auto top_desc = top_blob->GetBlobDesc();
            auto top_dims = top_desc.dims;
            auto top_data = top_blob->GetHandle().base;

            const int top_width = top_dims.at(1);

            for (int j = 0; j < batch; j++) {
                auto bottom_row = GET_OFFSET_PTR(bottom_data, j * bottom_width * data_type_bytes);
                auto top_row    = GET_OFFSET_PTR(top_data, j * top_width * data_type_bytes);

                memcpy(top_row, GET_OFFSET_PTR(bottom_row, passed_len * data_type_bytes),
                       top_width * data_type_bytes);
            }

            passed_len += top_width;
        }
    } else {
        return Status(COSMO_NN_ERR_PARAM, "SplitNode: Only support 2D blob");
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn