#include "nn/node/split_arg_max_node.h"

#include <algorithm>
#include <numeric>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

SplitArgMaxNode::SplitArgMaxNode() : Node() {
    node_type     = NodeType::NODE_SPLIT_ARG_MAX;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_SPLIT_ARG_MAX).append("_0");
    one_blob_only = false;
}

SplitArgMaxNode::~SplitArgMaxNode() {}

void SplitArgMaxNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto split_arg_max_op = dynamic_cast<SplitArgMax*>(op);
    split                 = split_arg_max_op->split;
}

size_t SplitArgMaxNode::GetBottomCount() {
    return 1;
}

size_t SplitArgMaxNode::GetTopCount() {
    return 1;
}

Status SplitArgMaxNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, 2, static_cast<int>(split.size())}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};

    return COSMO_NN_OK;
}

Status SplitArgMaxNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dims   = bottom_desc.dims;

    if (bottom_dims.size() != 2)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid bottom dims");

    const int batch = bottom_dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    SetCurrentBatch(top_blob, batch);

    // check split
    if (split.size() == 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "split size is 0");

    if (std::any_of(split.begin(), split.end(), [](int i) { return i < 0; }))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid negative split size");

    int total_size = std::accumulate(split.begin(), split.end(), 0);
    if (total_size > bottom_dims.at(1))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid split size");

    auto top_desc   = top_blob->GetBlobDesc();
    auto top_dims   = top_desc.dims;
    auto top_handle = top_blob->GetHandle();

    auto bottom_data = reinterpret_cast<float*>(bottom_handle.base);
    auto top_data    = reinterpret_cast<float*>(top_handle.base);

    int bottom_offset = DimsVectorUtils::Count(bottom_dims, 1);
    int top_offset    = DimsVectorUtils::Count(top_dims, 1);

    const int split_size = split.size();
    for (int i = 0; i < batch; i++) {
        int processed_size = 0;

        for (int j = 0; j < split_size; j++) {
            const int s = split.at(j);

            auto start_data = bottom_data + processed_size;
            auto max_iter   = std::max_element(start_data, start_data + s);
            int part_index  = std::distance(start_data, max_iter);

            top_data[j]              = part_index + processed_size;
            top_data[j + split_size] = *max_iter;

            processed_size += s;
        }

        bottom_data += bottom_offset;
        top_data += top_offset;
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn