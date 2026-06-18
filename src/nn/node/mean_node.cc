#include "nn/node/mean_node.h"

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

MeanNode::MeanNode() : Node() {
    node_type     = NodeType::NODE_MEAN;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_MEAN).append("_0");
    one_blob_only = false;
}

MeanNode::~MeanNode() {}

size_t MeanNode::GetTopCount() {
    return 1;
}

size_t MeanNode::GetBottomCount() {
    return bottom_blob_names.size();
}

bool MeanNode::NeedBottomShapesInfered() {
    return true;
}

Status MeanNode::InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) {
    // check bottom dims/data types
    auto dims_count = dims.size();
    auto type_count = types.size();
    if (dims_count != type_count)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "dims count != type count");

    auto dim            = dims.at(0);
    const DataType type = types.at(0);

    for (int i = 1; i < dims_count; ++i) {
        if (!DimsVectorUtils::Equal(dim, dims.at(i)))
            return Status(COSMO_NN_ERR_INVALID_INPUT, "dims not equal");

        if (types.at(i) != type)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "dims data type != type");
    }

    top_blob_shapes     = {dim};
    top_blob_data_types = {type};
    return COSMO_NN_OK;
}

Status MeanNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                         std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blobs, top_blobs, true));

    int batch              = 0;
    auto bottom_blob_count = bottom_blobs.size();
    for (int i = 0; i < bottom_blob_count; ++i) {
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

    auto top_dims   = top_blob->GetBlobDesc().dims;
    auto top_handle = top_blob->GetHandle();

    auto top_data_ptr = reinterpret_cast<float*>(top_handle.base);

    const int count = DimsVectorUtils::Count(top_dims);
    for (int i = 0; i < count; ++i) {
        top_data_ptr[i] = 0;
        for (int j = 0; j < bottom_blob_count; ++j) {
            auto bottom_data_ptr = reinterpret_cast<float*>(bottom_blobs.at(j)->GetHandle().base);

            top_data_ptr[i] += bottom_data_ptr[i] / bottom_blob_count;
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
