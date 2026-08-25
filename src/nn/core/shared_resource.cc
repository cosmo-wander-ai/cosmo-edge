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
    if (device == nullptr || device->GetHandle() == nullptr) {
        throw std::runtime_error("Sophon shared device handle is unavailable");
    }

    // Borrow the process-lifetime handle. A graph is routinely destroyed when
    // the last task using a model stops, while video decoding may still be
    // active for other tasks on the channel. Owning a separate handle here and
    // calling bm_dev_free during that graph teardown invalidates media state on
    // BM1688 runtimes and leaves subsequent preview frames solid green.
    m_handle = device->GetHandle();
#endif
}

SharedResource::~SharedResource() = default;

}  // namespace cosmo::nn
