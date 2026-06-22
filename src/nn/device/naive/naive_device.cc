#include "nn/device/naive/naive_device.h"

#include <cstring>

#include "nn/core/macros.h"
#include "nn/device/naive/naive_context.h"
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
    if (handle) {
        auto size = GetBlobMemoryBytesSize(size_info_);
        if (size > 0) {
            *handle = malloc(static_cast<size_t>(size));
            if (*handle && size > 0) {
                memset(*handle, 0, static_cast<size_t>(size));
            }
        } else if (size == 0) {
            *handle = NULL;
        } else {
            return Status(COSMO_NN_ERR_PARAM,
                          "NaiveDevice::Allocate invalid blob size (negative or > 512MB)");
        }
    }

    return COSMO_NN_OK;
}

Status NaiveDevice::Free(void* handle, unsigned long phy) {
    if (handle) {
        free(handle);
    }
    return COSMO_NN_OK;
}

Status NaiveDevice::CopyToDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    auto size_info       = Calculate(desc);
    size_t size_in_bytes = GetBlobMemoryBytesSize(size_info);

    memcpy(reinterpret_cast<char*>(dst->base), reinterpret_cast<char*>(src->base), size_in_bytes);
    return COSMO_NN_OK;
}

Status NaiveDevice::CopyFromDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) {
    auto size_info       = Calculate(desc);
    size_t size_in_bytes = GetBlobMemoryBytesSize(size_info);

    memcpy(reinterpret_cast<char*>(dst->base), reinterpret_cast<char*>(src->base), size_in_bytes);
    return COSMO_NN_OK;
}

AbstractContext* NaiveDevice::CreateContext(int device_id_) {
    return new NaiveContext();
}

TypeDeviceRegister<NaiveDevice> g_naive_device_register(DEVICE_NAIVE);

}  // namespace cosmo::nn