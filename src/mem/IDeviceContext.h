/// @file IDeviceContext.h
/// @brief Device context interface — provides access to BM1688 SoC device handles.
///
/// Lives in the Foundation layer (mem) so that both mem and upper layers can
/// depend on it without creating upward dependency violations.
#pragma once

namespace cosmo::mem {

/// Provides access to BM1688 SoC device handles (memory + media).
///
/// The BM1688 SoC exposes hardware resources through bm_handle_t handles that
/// are inherently process-global (one device per SoC). This interface wraps
/// those handles so that consumer layers (media, mem, flow) access them
/// through dependency injection rather than direct coupling.
///
/// Lifecycle is managed in application.cc: created before SwDeviceInit(),
/// destroyed after SwDeviceDestroy().
class IDeviceContext {
public:
    virtual ~IDeviceContext() = default;

    /// Returns the device memory handle (bm_handle_t as void*).
    /// Used by memory pool and frame allocation operations.
    [[nodiscard]] virtual void* GetMemoryHandle() = 0;

    /// Returns the device media handle (bm_handle_t as void*).
    /// Used by VPU decode, encode, and image processing operations.
    [[nodiscard]] virtual void* GetMediaHandle() = 0;
};

}  // namespace cosmo::mem
