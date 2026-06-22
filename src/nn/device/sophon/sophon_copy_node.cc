#include "nn/device/sophon/sophon_copy_node.h"

#include <string.h>

#include <algorithm>
#include <cstdio>
#include <iostream>

#include "bmruntime_cpp.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/string_format.h"

namespace cosmo::nn {

SophonCopyNode::SophonCopyNode() : CopyNode() {
    name = NodeTypeUtils::NodeTypeToStr(NodeType::NODE_COPY).append("_0");
}

SophonCopyNode::~SophonCopyNode() {}

void SophonCopyNode::LoadParam(Op *op) {}

void SophonCopyNode::SetDirection(int from, int to) {
    this->from = from;
    this->to   = to;
}

int SophonCopyNode::GetFrom() {
    return from;
}

int SophonCopyNode::GetTo() {
    return to;
}

Status SophonCopyNode::InferTopShapesWithBottoms(std::vector<DimsVector> bottoms_dims,
                                                 std::vector<DataType> bottoms_types) {
    top_blob_shapes     = {bottoms_dims.at(0)};
    top_blob_data_types = {bottoms_types.at(0)};
    return COSMO_NN_OK;
}

DeviceType SophonCopyNode::GetTopBlobDeviceType() {
    if (to == 0)
        return DeviceType::DEVICE_NAIVE;
    else
        return DeviceType::DEVICE_SOPHON_TPU;
}

Status SophonCopyNode::Forward(std::vector<std::shared_ptr<Blob>> &bottom_blobs,
                               std::vector<std::shared_ptr<Blob>> &top_blobs) {
    bm_status_t ret       = BM_SUCCESS;
    bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

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

    // top blob set current shape
    auto top_desc   = top_blob->GetBlobDesc();
    auto top_handle = top_blob->GetHandle();
    auto top_dim    = top_desc.dims;
    top_dim.at(0)   = batch;
    top_desc.dims   = top_dim;
    top_blob->SetBlobDesc(top_desc);

    if (DataTypeUtils::GetBytesSize(bottom_desc.data_type) !=
        DataTypeUtils::GetBytesSize(top_desc.data_type)) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid data type");
    }

    // calculate size
    int element_num = DimsVectorUtils::Count(bottom_dim);
    if (element_num != DimsVectorUtils::Count(top_dim))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid shape");

    size_t bytes_size = element_num * DataTypeUtils::GetBytesSize(bottom_desc.data_type);

    void *src = bottom_handle.base;
    void *dst = top_handle.base;

    if (from == 0 && to == 1) {  // copy from cpu to gpu
        bm_device_mem_t *dst_dev_mem = reinterpret_cast<bm_device_mem_t *>(dst);
        if (bm_memcpy_s2d_partial(pbmhandle, *dst_dev_mem, src, bytes_size) != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy s2d failed");
        }
    } else if (from == 0 && to == 0) {  // copy from cpu to cpu
        memcpy(dst, src, bytes_size);
    } else if (from == 1 && to == 0) {  // copy from gpu to cpu
        bm_device_mem_t *src_dev_mem = reinterpret_cast<bm_device_mem_t *>(src);

        if (bm_memcpy_d2s_partial(pbmhandle, dst, *src_dev_mem, bytes_size) != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2s failed");
        }

    } else if (from == 1 && to == 1) {  // copy from gpu to gpu
        bm_device_mem_t *src_dev_mem = reinterpret_cast<bm_device_mem_t *>(src);
        bm_device_mem_t *dst_dev_mem = reinterpret_cast<bm_device_mem_t *>(dst);
        if (bm_memcpy_d2d_byte(pbmhandle, *dst_dev_mem, 0, *src_dev_mem, 0, bytes_size) != BM_SUCCESS) {
            return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "Sophon memcpy d2d failed");
        }
    } else {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid from and to");
    }
    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn