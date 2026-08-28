#include "nn/core/shared_resource.h"

#include <stdexcept>

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "nn/core/abstract_device.h"
#include "nn/device/sophon/sophon_device.h"
#endif

namespace cosmo::nn {

SharedResource::SharedResource(int id) {
    current_device_id = id;

#ifdef COSMO_NN_USE_SOPHON_BACKEND
    auto* device = dynamic_cast<SophonDevice*>(GetDevice(DEVICE_SOPHON_TPU));
    if (device == nullptr) {
        throw std::runtime_error("Sophon runtime handle owner is unavailable");
    }

    // Preserve the original one-handle-per-active-graph concurrency boundary,
    // but keep idle handles in the process-wide pool. On deployed BM1688
    // runtimes, immediately closing this graph/BMRT handle during task teardown
    // is correlated with invalid active media state and solid-green previews.
    m_handle             = device->AcquireRuntimeHandle(id);
    sophon_device_owner_ = device;
#endif
}

SharedResource::~SharedResource() {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (sophon_device_owner_ != nullptr && m_handle != nullptr) {
        sophon_device_owner_->ReleaseRuntimeHandle(m_handle);
        m_handle = nullptr;
    }
#endif
}

}  // namespace cosmo::nn
