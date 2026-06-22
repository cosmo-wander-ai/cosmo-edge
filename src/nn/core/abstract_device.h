#pragma once

#include <memory>

#include "nn/core/abstract_context.h"
#include "nn/core/blob.h"
#include "nn/core/common.h"
#include "nn/core/status.h"
#include "nn/utils/blob_memory_size_info.h"

namespace cosmo::nn {

// create memory and context
class AbstractDevice {
public:
    explicit AbstractDevice(DeviceType type);

    virtual ~AbstractDevice(){};

    virtual BlobMemorySizeInfo Calculate(BlobDesc& desc) = 0;

    virtual AbstractContext* CreateContext(int device_id_) = 0;

    virtual Status Allocate(void** handle, unsigned long* phy, BlobMemorySizeInfo& size_info_) = 0;

    virtual Status Allocate(BlobHandle* handle, BlobMemorySizeInfo& size_info_);

    virtual Status Free(void* handle, unsigned long phy) = 0;

    virtual Status Free(const BlobHandle& handle);

    virtual Status CopyToDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) = 0;

    virtual Status CopyFromDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) = 0;

    DeviceType GetDeviceType();

protected:
    DeviceType device_type;
};

std::map<DeviceType, std::shared_ptr<AbstractDevice>>& GetGlobDeviceMap();

AbstractDevice* GetDevice(DeviceType type);

template <typename T>
class TypeDeviceRegister {
public:
    explicit TypeDeviceRegister(DeviceType type) {
        auto& map = GetGlobDeviceMap();
        if (map.find(type) == map.end()) {
            map[type] = std::shared_ptr<T>(new T(type));
        }
    }
};

}  // namespace cosmo::nn
