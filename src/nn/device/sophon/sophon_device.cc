#include "nn/device/sophon/sophon_device.h"

#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

#include "nn/device/sophon/sophon_context.h"
#include "nn/node/node.h"
#include "nn/utils/blob_memory_size_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

SophonDevice::SophonDevice(DeviceType device_type_) : AbstractDevice(device_type_) {
    const int ret = bm_dev_request(&bm_handle, 0);
    if (ret != BM_SUCCESS || bm_handle == nullptr) {
        bm_handle = nullptr;
        throw std::runtime_error("SophonDevice bm_dev_request failed");
    }
}

SophonDevice::~SophonDevice() {
    // Runtime handles are deliberately retained until device-registry teardown
    // so ordinary task stop/start never closes a graph BM context while media
    // pipelines are still active.
    {
        std::lock_guard<std::mutex> lock(runtime_handles_mutex_);
        for (auto& entry : runtime_handles_) {
            if (entry.handle != nullptr) {
                bm_dev_free(entry.handle);
                entry.handle = nullptr;
            }
        }
        runtime_handles_.clear();
    }

    if (bm_handle) {
        bm_dev_free(bm_handle);
        bm_handle = nullptr;
    }
}

bm_handle_t SophonDevice::AcquireRuntimeHandle(int device_id) {
    std::lock_guard<std::mutex> lock(runtime_handles_mutex_);
    for (auto& entry : runtime_handles_) {
        if (!entry.in_use && entry.device_id == device_id && entry.handle != nullptr) {
            entry.in_use = true;
            return entry.handle;
        }
    }

    bm_handle_t handle = nullptr;
    const int ret      = bm_dev_request(&handle, device_id);
    if (ret != BM_SUCCESS || handle == nullptr) {
        throw std::runtime_error("Sophon runtime handle request failed");
    }

    try {
        runtime_handles_.push_back({handle, device_id, true});
    } catch (...) {
        bm_dev_free(handle);
        throw;
    }
    return handle;
}

void SophonDevice::ReleaseRuntimeHandle(bm_handle_t handle) noexcept {
    if (handle == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(runtime_handles_mutex_);
    for (auto& entry : runtime_handles_) {
        if (entry.handle == handle) {
            entry.in_use = false;
            return;
        }
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
    if (handle == nullptr || phy == nullptr || bm_handle == nullptr) {
        return Status(COSMO_NN_ERR_NULL_PARAM, "SophonDevice::Allocate invalid handle");
    }
    *handle = nullptr;
    *phy    = 0;

    const int64_t bytes_size = GetBlobMemoryBytesSize(size_info_);
    if (bytes_size <= 0 || bytes_size > 512L * 1024 * 1024) {
        return Status(COSMO_NN_ERR_PARAM, "SophonDevice::Allocate invalid blob size");
    }

    auto tensor_mem = std::unique_ptr<bm_device_mem_t>(new (std::nothrow) bm_device_mem_t{});
    if (!tensor_mem) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "SophonDevice descriptor allocation failed");
    }
    const bm_status_t ret = bm_malloc_device_byte_heap_mask(bm_handle, tensor_mem.get(), 3,
                                                            static_cast<unsigned int>(bytes_size));
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "sophon malloc device mem failed.");
    }

    *handle = static_cast<void*>(tensor_mem.release());  // transfer ownership to caller
    return COSMO_NN_OK;
}

Status SophonDevice::Free(void* handle, unsigned long phy) {
    if (handle == nullptr) {
        return COSMO_NN_OK;
    }
    if (bm_handle == nullptr) {
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "SophonDevice::Free device handle is null");
    }

    bm_device_mem_t* mem = static_cast<bm_device_mem_t*>(handle);
    bm_free_device(bm_handle, *mem);
    delete mem;

    return COSMO_NN_OK;
}

Status SophonDevice::Allocate(void** handle, size_t size) {
    if (handle == nullptr || size == 0 || size > 512U * 1024 * 1024 || bm_handle == nullptr) {
        return Status(COSMO_NN_ERR_PARAM, "SophonDevice::Allocate invalid raw allocation");
    }
    *handle         = nullptr;
    auto tensor_mem = std::unique_ptr<bm_device_mem_t>(new (std::nothrow) bm_device_mem_t{});
    if (!tensor_mem) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "SophonDevice descriptor allocation failed");
    }
    const auto ret =
        bm_malloc_device_byte_heap_mask(bm_handle, tensor_mem.get(), 3, static_cast<unsigned int>(size));
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_ALLOC_MEM_FAILED, "SophonDevice raw allocation failed");
    }
    *handle = tensor_mem.release();
    return COSMO_NN_OK;
}

Status SophonDevice::CopyToDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    if (dst == nullptr || src == nullptr || dst->base == nullptr || src->base == nullptr ||
        bm_handle == nullptr) {
        return Status(COSMO_NN_ERR_NULL_PARAM, "SophonDevice::CopyToDevice invalid handle");
    }
    auto size_info          = Calculate(desc);
    const int64_t byte_size = GetBlobMemoryBytesSize(size_info);
    if (byte_size <= 0 || byte_size > std::numeric_limits<unsigned int>::max()) {
        return Status(COSMO_NN_ERR_PARAM, "SophonDevice::CopyToDevice invalid size");
    }
    auto* dst_mem = static_cast<bm_device_mem_t*>(dst->base);
    if (bm_mem_get_device_size(*dst_mem) < static_cast<unsigned int>(byte_size) ||
        bm_memcpy_s2d_partial(bm_handle, *dst_mem, src->base, static_cast<unsigned int>(byte_size)) !=
            BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "SophonDevice copy to device failed");
    }
    return COSMO_NN_OK;
}

Status SophonDevice::CopyFromDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    if (dst == nullptr || src == nullptr || dst->base == nullptr || src->base == nullptr ||
        bm_handle == nullptr) {
        return Status(COSMO_NN_ERR_NULL_PARAM, "SophonDevice::CopyFromDevice invalid handle");
    }
    auto size_info          = Calculate(desc);
    const int64_t byte_size = GetBlobMemoryBytesSize(size_info);
    if (byte_size <= 0 || byte_size > std::numeric_limits<unsigned int>::max()) {
        return Status(COSMO_NN_ERR_PARAM, "SophonDevice::CopyFromDevice invalid size");
    }
    auto* src_mem = static_cast<bm_device_mem_t*>(src->base);
    if (bm_mem_get_device_size(*src_mem) < static_cast<unsigned int>(byte_size) ||
        bm_memcpy_d2s_partial(bm_handle, dst->base, *src_mem, static_cast<unsigned int>(byte_size)) !=
            BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "SophonDevice copy from device failed");
    }
    return COSMO_NN_OK;
}

TypeDeviceRegister<SophonDevice> g_sophon_device_register(DEVICE_SOPHON_TPU);
}  // namespace cosmo::nn
