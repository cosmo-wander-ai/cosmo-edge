#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_copy_node.h"

#include <cstring>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

CpuCopyNode::CpuCopyNode() : CopyNode() {
    name = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_COPY).append("_0");
}

void CpuCopyNode::SetDirection(int from, int to) {
    from_ = from;
    to_   = to;
}

int CpuCopyNode::GetFrom() {
    return from_;
}

int CpuCopyNode::GetTo() {
    return to_;
}

DeviceType CpuCopyNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

void CpuCopyNode::LoadParam(Op* /*op*/) {}

Status CpuCopyNode::InferTopShapesWithBottoms(std::vector<DimsVector> bottoms_dims,
                                              std::vector<DataType> bottoms_types) {
    top_blob_shapes     = {bottoms_dims.at(0)};
    top_blob_data_types = {bottoms_types.at(0)};
    return COSMO_NN_OK;
}

Status CpuCopyNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                            std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, false));

    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dim    = bottom_desc.dims;

    // check batch
    int batch = bottom_dim.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size is bigger than max_batch size");

    // set current shape on top blob
    auto top_desc = top_blob->GetBlobDesc();
    auto top_dim  = top_desc.dims;
    top_dim.at(0) = batch;
    top_desc.dims = top_dim;
    top_blob->SetBlobDesc(top_desc);

    auto top_handle = top_blob->GetHandle();

    if (DataTypeUtils::GetBytesSize(bottom_desc.data_type) !=
        DataTypeUtils::GetBytesSize(top_desc.data_type)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid data type");
    }

    int element_num = DimsVectorUtils::Count(bottom_dim);
    if (element_num != DimsVectorUtils::Count(top_dim))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid shape");

    size_t bytes_size = element_num * DataTypeUtils::GetBytesSize(bottom_desc.data_type);

    void* src = bottom_handle.base;
    void* dst = top_handle.base;

    // CPU-only mode: always host-to-host memcpy
    if (src != dst) {
        std::memcpy(dst, src, bytes_size);
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
