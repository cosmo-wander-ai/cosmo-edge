#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <memory>

#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_face_model_binding.h"
#include "nn/device/axera/axera_raw_yuv_executor.h"

namespace cosmo::nn::axera {
// Low-level synchronous adapter, not an admission or application API.
// Caller must already verify same-frame identity and color provenance.
// Never accepts host image buffers and never runs CPU NMS.
class FaceNativeRunner {
public:
    static RawYuvResult Run(const std::shared_ptr<const face_binding::Capability>& capability,
                            media::NativeVideoBufferPtr source, float scene_threshold);
    static RawYuvResult RunJfif(const std::shared_ptr<const face_binding::Capability>& capability,
                                media::NativeVideoBufferPtr source, float scene_threshold);

private:
    static RawYuvResult RunForColor(const std::shared_ptr<const face_binding::Capability>& capability,
                                    media::NativeVideoBufferPtr source, float scene_threshold,
                                    raw_yuv::ColorProfile color);
};
}  // namespace cosmo::nn::axera
#endif
