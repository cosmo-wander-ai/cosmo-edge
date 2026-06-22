#pragma once

#include <vector>

#include "mem/IDeviceContext.h"

namespace cosmo::mem {

/// NPU device context — manages BM1688 SoC device handles (memory + media).
///
/// Lifecycle is explicitly managed in application.cc: created before
/// SwDeviceInit(), registered via ServiceRegistry, destroyed after
/// SwDeviceDestroy().
class DeviceContext : public IDeviceContext {
public:
    DeviceContext();
    ~DeviceContext() override;

    DeviceContext(const DeviceContext&)            = delete;
    DeviceContext& operator=(const DeviceContext&) = delete;

    [[nodiscard]] void* GetMemoryHandle() override;
    [[nodiscard]] void* GetMediaHandle() override;

private:
    std::vector<void*> handles_;
    const int dev_id_ = 0;
};

}  // namespace cosmo::mem
