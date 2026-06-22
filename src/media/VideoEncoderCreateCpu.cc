// VideoEncoderCreateCpu.cc — CPU backend factory for VideoEncoder.
// Compiled only when COSMO_NN_USE_CPU_BACKEND is ON (CMake file-level switching).

#include "media/VideoEncoder.h"
#include "media/VideoEncoderCpu.h"

namespace cosmo {
namespace media {

    std::shared_ptr<VideoEncoder> VideoEncoder::Create(void* mediaHandle) {
        static_cast<void>(mediaHandle);  // CPU backend doesn't need device handle
        return std::make_shared<VideoEncoderCpu>();
    }

}  // namespace media
}  // namespace cosmo
