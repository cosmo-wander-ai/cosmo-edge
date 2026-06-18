#pragma once

namespace cosmo {
namespace media {

    enum class PixelBaseType {
        PIXEL_UINT8,
        PIXEL_FLOAT,
    };

    enum class PixelFormat {
        // RGB
        PIXEL_RGB8 = 0,
        PIXEL_RGBA8,
        PIXEL_RGB32F,
        PIXEL_RGBA32F,

        // BGR
        PIXEL_BGR8,
        PIXEL_BGRA8,
        PIXEL_BGR32F,
        PIXEL_BGRA32F,

        // YUV
        PIXEL_YUYV,  // 4:2:2 packed
        PIXEL_YVYU,
        PIXEL_UYVY,
        PIXEL_I420,
        PIXEL_NV12,
        PIXEL_NV21,

        PIXEL_FORMAT_COUNT,
        PIXEL_UNKNOWN = 99,
        PIXEL_DEFAULT = PIXEL_RGB8,
    };

}  // namespace media
}  // namespace cosmo
