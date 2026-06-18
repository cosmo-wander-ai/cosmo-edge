// VideoEncoderCreateSophon.cc — Sophon backend factory for VideoEncoder.
// Compiled only when COSMO_NN_USE_SOPHON_BACKEND is ON (CMake file-level switching).

#include "media/VideoEncoder.h"
#include "media/VideoEncoderSophon.h"

namespace cosmo {
namespace media {

    std::shared_ptr<VideoEncoder> VideoEncoder::Create(void* mediaHandle) {
        return std::make_shared<VideoEncoderSophon>(mediaHandle);
    }

}  // namespace media
}  // namespace cosmo
