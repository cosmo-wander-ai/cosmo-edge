#include "nn/device/sophon/sophon_device.h"

#include <iostream>

#include "nn/device/sophon/sophon_context.h"
#include "nn/node/node.h"
#include "nn/utils/blob_memory_size_utils.h"
#include "nn/utils/dims_vector_utils.h"
namespace cosmo::nn {

SophonDevice::SophonDevice(DeviceType device_type_) : AbstractDevice(device_type_) {
    int ret = bm_dev_request(&bm_handle, 0);
    assert(BM_SUCCESS == ret);
}

SophonDevice::~SophonDevice() {
    // free handle
    if (bm_handle) {
        bm_dev_free(bm_handle);
    }
}

AbstractContext* SophonDevice::CreateContext(int device_id_) {
    return nullptr;
}

BlobMemorySizeInfo SophonDevice::Calculate(BlobDesc& desc) {
    auto size_info = Calculate1DMemorySize(desc);
    int size_count = DimsVectorUtils::Count(size_info.dims);
    if (size_count == 0) {
        size_info.dims[0] = 1;
    }
    return size_info;
}

Status SophonDevice::Allocate(void** handle, unsigned long* phy, BlobMemorySizeInfo& size_info_) {
    bm_status_t ret    = BM_SUCCESS;
    int64_t bytes_size = GetBlobMemoryBytesSize(size_info_);
    if (bytes_size < 0 || bytes_size > 512L * 1024 * 1024) {
        return Status(COSMO_NN_ERR_PARAM, "SophonDevice::Allocate invalid blob size (negative or > 512MB)");
    }

    auto tensor_mem = std::make_unique<bm_device_mem_t>();
    ret = bm_malloc_device_byte_heap_mask(bm_handle, tensor_mem.get(), 3, static_cast<size_t>(bytes_size));
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "sophon malloc device mem failed.");
    }

    *handle = static_cast<void*>(tensor_mem.release());  // transfer ownership to caller
    return COSMO_NN_OK;
}

Status SophonDevice::Free(void* handle, unsigned long phy) {
    // free memory
    bm_device_mem_t* mem = static_cast<bm_device_mem_t*>(handle);
    bm_free_device(bm_handle, *mem);
    delete mem;

    return COSMO_NN_OK;
}

Status SophonDevice::Allocate(void** handle, size_t size) {
    std::cout << "SophonDevice::Allocate empty!" << std::endl;
    return COSMO_NN_OK;
}

Status SophonDevice::CopyToDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    std::cout << "SophonDevice::CopyToDevice empty!" << std::endl;
    return COSMO_NN_OK;
}

Status SophonDevice::CopyFromDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    std::cout << "SophonDevice::CopyFromDevice empty!" << std::endl;
    return COSMO_NN_OK;
}

TypeDeviceRegister<SophonDevice> g_sophon_device_register(DEVICE_SOPHON_TPU);
}  // namespace cosmo::nn