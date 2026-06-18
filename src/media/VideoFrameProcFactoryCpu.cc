// VideoFrameProcFactoryCpu.cc — CPU backend factory for IVideoFrameProc.
// Compiled only when COSMO_NN_USE_CPU_BACKEND is ON (CMake file-level switching).

#include "media/IOsdTextRenderer.h"
#include "media/VideoFrameProcCpu.h"
#include "media/VideoFrameProcFactory.h"
#include "mem/IDeviceContext.h"

namespace cosmo {
namespace media {

    std::unique_ptr<IVideoFrameProc> CreateVideoFrameProc(mem::IDeviceContext& ctx, IOsdTextRenderer& osd) {
        static_cast<void>(ctx);  // CPU backend doesn't need device handle
        return std::make_unique<VideoFrameProcCpu>(osd);
    }

}  // namespace media
}  // namespace cosmo
