// VideoDecoderCreateCpu.cc — CPU backend factory for VideoDecoder.
// Compiled only when COSMO_NN_USE_CPU_BACKEND is ON (CMake file-level switching).

#include "media/VideoDecoder.h"
#include "media/VideoDecoderCpu.h"

namespace cosmo {
namespace media {

    std::unique_ptr<VideoDecoder> VideoDecoder::Create(size_t name, void* mediaHandle) {
        static_cast<void>(mediaHandle);  // CPU backend doesn't need device handle
        return std::make_unique<VideoDecoderCpu>(name);
    }

}  // namespace media
}  // namespace cosmo
