/// @file VideoFrameProcFactory.h
/// @brief Factory function for creating the correct backend VideoFrameProc.
///
/// The concrete implementation (Sophon or CPU) is selected at CMake time —
/// only one of VideoFrameProcFactorySophon.cc or VideoFrameProcFactoryCpu.cc
/// is compiled into the binary.
#pragma once

#include <memory>

#include "media/IVideoFrameProc.h"

namespace cosmo {
namespace mem {
    class IDeviceContext;
}
namespace media {
    class IOsdTextRenderer;

    /// Creates the correct backend VideoFrameProc (Sophon or CPU).
    /// @param ctx  Device context providing hardware handles
    /// @param osd  OSD text rendering service
    std::unique_ptr<IVideoFrameProc> CreateVideoFrameProc(mem::IDeviceContext& ctx, IOsdTextRenderer& osd);

}  // namespace media
}  // namespace cosmo
