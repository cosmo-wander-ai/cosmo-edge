#pragma once

#include <cstddef>

#include "media/PixelFormat.h"

namespace cosmo {
namespace media {

    class PixelFormatUtils {
    public:
        // format
        static PixelBaseType PixelFormatBaseType(PixelFormat);

        static size_t PixelFormatChannels(PixelFormat);

        static size_t PixelFormatDepth(PixelFormat);

        static bool PixelFormatIsRGB(PixelFormat);

        static bool PixelFormatIsBGR(PixelFormat);

        static bool PixelFormatIsYUV(PixelFormat);
    };

}  // namespace media
}  // namespace cosmo
