#include "nn/device/naive/naive_device.h"

#include <cstring>

#include "nn/core/macros.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

NaiveDevice::NaiveDevice(DeviceType device_type_) : AbstractDevice(device_type_) {}

NaiveDevice::~NaiveDevice() {}

BlobMemorySizeInfo NaiveDevice::Calculate(BlobDesc& desc) {
    BlobMemorySizeInfo info;
    info.data_type = desc.data_type;
    int count      = DimsVectorUtils::Count(desc.dims);
    info.dims.push_back(count);
    return info;
}

Status NaiveDevice::Allocate(void** handle, unsigned long* phy, BlobMemorySizeInfo& size_info_) {
    if (handle == nullptr || phy == nullptr) {
        return Status(COSMO_NN_ERR_NULL_PARAM, "NaiveDevice::Allocate invalid handle");
    }
    *handle = nullptr;
    *phy    = 0;

    const auto size = GetBlobMemoryBytesSize(size_info_);
    if (size <= 0) {
        return Status(COSMO_NN_ERR_PARAM, "NaiveDevice::Allocate invalid blob size");
    }

    *handle = malloc(static_cast<size_t>(size));
    if (*handle == nullptr) {
        return Status(COSMO_NN_ERR_OUT_OF_MEMORY, "NaiveDevice::Allocate host allocation failed");
    }
    memset(*handle, 0, static_cast<size_t>(size));

    return Status{COSMO_NN_OK};
}

Status NaiveDevice::Free(void* handle, unsigned long phy) {
    if (handle) {
        free(handle);
    }
    return Status{COSMO_NN_OK};
}

Status NaiveDevice::CopyToDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    if (dst == nullptr || src == nullptr || dst->base == nullptr || src->base == nullptr) {
        return Status(COSMO_NN_ERR_NULL_PARAM, "NaiveDevice::CopyToDevice invalid handle");
    }
    auto size_info              = Calculate(desc);
    const int64_t size_in_bytes = GetBlobMemoryBytesSize(size_info);
    if (size_in_bytes <= 0) {
        return Status(COSMO_NN_ERR_PARAM, "NaiveDevice::CopyToDevice invalid size");
    }

    memcpy(reinterpret_cast<char*>(dst->base), reinterpret_cast<char*>(src->base),
           static_cast<size_t>(size_in_bytes));
    return Status{COSMO_NN_OK};
}

Status NaiveDevice::CopyFromDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    if (dst == nullptr || src == nullptr || dst->base == nullptr || src->base == nullptr) {
        return Status(COSMO_NN_ERR_NULL_PARAM, "NaiveDevice::CopyFromDevice invalid handle");
    }
    auto size_info              = Calculate(desc);
    const int64_t size_in_bytes = GetBlobMemoryBytesSize(size_info);
    if (size_in_bytes <= 0) {
        return Status(COSMO_NN_ERR_PARAM, "NaiveDevice::CopyFromDevice invalid size");
    }

    memcpy(reinterpret_cast<char*>(dst->base), reinterpret_cast<char*>(src->base),
           static_cast<size_t>(size_in_bytes));
    return Status{COSMO_NN_OK};
}

TypeDeviceRegister<NaiveDevice> g_naive_device_register(DEVICE_NAIVE);

}  // namespace cosmo::nn
