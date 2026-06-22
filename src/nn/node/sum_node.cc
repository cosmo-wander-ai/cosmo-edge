#include "nn/node/sum_node.h"

#include <numeric>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

SumNode::SumNode() : Node() {
    node_type     = NodeType::NODE_SUM;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_SUM).append("_0");
    one_blob_only = true;
}

SumNode::~SumNode() {}

size_t SumNode::GetTopCount() {
    return 1;
}

size_t SumNode::GetBottomCount() {
    return 1;
}

Status SumNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, 1}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};

    return COSMO_NN_OK;
}

Status SumNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                        std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    if (bottom_blobs.size() != 1 || top_blobs.size() != 1)
        return Status(COSMO_NN_ERR_INVALID_INPUT,
                      "SumNode: bottom_blobs.size() != 1 || top_blobs.size() != 1");

    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

    // check batch
    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dims   = bottom_desc.dims;

    const int current_batch = bottom_dims.at(0);
    if (current_batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "SumNode: current batch is larger than max batch");

    SetCurrentBatch(top_blob, current_batch);

    auto bottom_data = reinterpret_cast<float*>(bottom_handle.base);
    auto top_data    = reinterpret_cast<float*>(top_blob->GetHandle().base);

    const int bottom_plane_size = DimsVectorUtils::Count(bottom_dims, 1);
    for (int i = 0; i < current_batch; i++) {
        top_data[i] = std::accumulate(bottom_data, bottom_data + bottom_plane_size, 0.0f);

        bottom_data += bottom_plane_size;
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn