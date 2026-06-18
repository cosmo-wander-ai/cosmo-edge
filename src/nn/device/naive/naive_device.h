#pragma once

#include "nn/core/abstract_device.h"

namespace cosmo::nn {

class NaiveDevice : public AbstractDevice {
public:
    explicit NaiveDevice(DeviceType device_type);

    ~NaiveDevice();

    virtual BlobMemorySizeInfo Calculate(BlobDesc& desc) override;

    virtual Status Allocate(void** handle, unsigned long* phy, BlobMemorySizeInfo& size_info_) override;

    virtual Status Free(void* handle, unsigned long phy) override;

    virtual AbstractContext* CreateContext(int device_id_) override;

    virtual Status CopyToDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) override;

    virtual Status CopyFromDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc,
                                  void* queue) override;
};
}  // namespace cosmo::nn
