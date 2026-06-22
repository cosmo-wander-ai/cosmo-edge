// VideoFrameProcFactorySophon.cc — Sophon backend factory for IVideoFrameProc.
// Compiled only when COSMO_NN_USE_SOPHON_BACKEND is ON (CMake file-level switching).

#include "media/IOsdTextRenderer.h"
#include "media/VideoFrameProcFactory.h"
#include "media/VideoFrameProcSophon.h"
#include "mem/IDeviceContext.h"

namespace cosmo {
namespace media {

    std::unique_ptr<IVideoFrameProc> CreateVideoFrameProc(mem::IDeviceContext& ctx, IOsdTextRenderer& osd) {
        return std::make_unique<VideoFrameProcSophon>(ctx.GetMediaHandle(), osd);
    }

}  // namespace media
}  // namespace cosmo
