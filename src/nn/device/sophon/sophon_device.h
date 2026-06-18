#pragma once

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_interface.h"
#include "nn/core/abstract_device.h"

namespace cosmo::nn {

class SophonDevice : public AbstractDevice {
public:
    using AbstractDevice::Allocate;

    explicit SophonDevice(DeviceType device_type_);

    ~SophonDevice();

    virtual BlobMemorySizeInfo Calculate(BlobDesc& desc) override;

    virtual Status Allocate(void** handle, unsigned long* phy, BlobMemorySizeInfo& size_info_) override;

    virtual Status Free(void* handle, unsigned long phy) override;

    virtual AbstractContext* CreateContext(int device_id_) override;

    virtual Status CopyToDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc, void* queue) override;

    virtual Status CopyFromDevice(BlobHandle* dst, const BlobHandle* src, BlobDesc& desc,
                                  void* queue) override;

    Status Allocate(void** handle, size_t size);

private:
    bm_handle_t bm_handle;
};

}  // namespace cosmo::nn
