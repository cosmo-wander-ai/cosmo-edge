#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <memory>

#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_fire_model_binding.h"
#include "nn/device/axera/axera_raw_yuv_executor.h"

namespace cosmo::nn::axera {
// Low-level synchronous adapter, not an admission or application API.
// Caller must already verify same-frame identity and color provenance.
// Never accepts host image buffers and never runs CPU NMS.
class FireNativeRunner {
public:
    static RawYuvFireResult Run(const std::shared_ptr<const fire_binding::Capability>& capability,
                                media::NativeVideoBufferPtr source);
    static RawYuvFireResult RunJfif(const std::shared_ptr<const fire_binding::Capability>& capability,
                                    media::NativeVideoBufferPtr source);

private:
    static RawYuvFireResult RunForColor(const std::shared_ptr<const fire_binding::Capability>& capability,
                                        media::NativeVideoBufferPtr source, raw_yuv::ColorProfile color);
};
}  // namespace cosmo::nn::axera
#endif
