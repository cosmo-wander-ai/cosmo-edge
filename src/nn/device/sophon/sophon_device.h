#pragma once

#include <mutex>
#include <vector>

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

    // Lease a graph-local BM handle. Concurrent graphs receive distinct handles
    // so bm_thread_sync remains isolated, while released handles stay alive and
    // are recycled instead of calling bm_dev_free during active media work.
    [[nodiscard]] bm_handle_t AcquireRuntimeHandle(int device_id);

    void ReleaseRuntimeHandle(bm_handle_t handle) noexcept;

private:
    struct RuntimeHandleEntry {
        bm_handle_t handle{nullptr};
        int device_id{0};
        bool in_use{false};
    };

    bm_handle_t bm_handle = nullptr;
    std::mutex runtime_handles_mutex_;
    std::vector<RuntimeHandleEntry> runtime_handles_;
};

}  // namespace cosmo::nn
