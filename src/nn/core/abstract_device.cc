#include "nn/core/abstract_device.h"

#include <mutex>

#include "nn/utils/blob_memory_size_info.h"

namespace cosmo::nn {

AbstractDevice::AbstractDevice(DeviceType device_type_) : device_type(device_type_) {}

Status AbstractDevice::Allocate(BlobHandle* handle, BlobMemorySizeInfo& size_info) {
    void* data        = nullptr;
    unsigned long phy = 0;

    RETURN_ON_FAIL(Allocate(&data, &phy, size_info));

    handle->base = data;
    handle->phy  = phy;

    return COSMO_NN_OK;
}

Status AbstractDevice::Free(const BlobHandle& handle) {
    return Free(handle.base, handle.phy);
}

DeviceType AbstractDevice::GetDeviceType() {
    return device_type;
}

std::map<DeviceType, std::shared_ptr<AbstractDevice>>& GetGlobDeviceMap() {
    static std::once_flag device_once;
    static std::shared_ptr<std::map<DeviceType, std::shared_ptr<AbstractDevice>>> device_map;

    std::call_once(device_once,
                   []() { device_map.reset(new std::map<DeviceType, std::shared_ptr<AbstractDevice>>()); });

    return *device_map;
}

AbstractDevice* GetDevice(DeviceType type) {
    return GetGlobDeviceMap()[type].get();
}

}  // namespace cosmo::nn