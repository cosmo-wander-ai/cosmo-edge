// VideoDecoderCreateSophon.cc — Sophon backend factory for VideoDecoder.
// Compiled only when COSMO_NN_USE_SOPHON_BACKEND is ON (CMake file-level switching).

#include "media/VideoDecoder.h"
#include "media/VideoDecoderSophon.h"

namespace cosmo {
namespace media {

    std::unique_ptr<VideoDecoder> VideoDecoder::Create(size_t name, void* mediaHandle) {
        return std::make_unique<VideoDecoderSophon>(name, mediaHandle);
    }

}  // namespace media
}  // namespace cosmo
