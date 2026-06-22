// PixelFormatUtils — Pixel Format Utils implementation.

#include "media/PixelFormatUtils.h"

#include <cstdint>

namespace cosmo {
namespace media {

    PixelBaseType PixelFormatUtils::PixelFormatBaseType(PixelFormat format) {
        switch (format) {
            case PixelFormat::PIXEL_BGR32F:
            case PixelFormat::PIXEL_BGRA32F:
            case PixelFormat::PIXEL_RGB32F:
            case PixelFormat::PIXEL_RGBA32F:
                return PixelBaseType::PIXEL_FLOAT;
            default:
                break;
        }

        return PixelBaseType::PIXEL_UINT8;
    }

    size_t PixelFormatUtils::PixelFormatChannels(PixelFormat f) {
        switch (f) {
            case PixelFormat::PIXEL_BGR8:
            case PixelFormat::PIXEL_BGR32F:
            case PixelFormat::PIXEL_RGB8:
            case PixelFormat::PIXEL_RGB32F:
                return 3;

            case PixelFormat::PIXEL_YUYV:
            case PixelFormat::PIXEL_YVYU:
            case PixelFormat::PIXEL_UYVY:
            case PixelFormat::PIXEL_I420:
            case PixelFormat::PIXEL_NV12:
            case PixelFormat::PIXEL_NV21:
                return 3;

            case PixelFormat::PIXEL_RGBA8:
            case PixelFormat::PIXEL_RGBA32F:
            case PixelFormat::PIXEL_BGRA8:
            case PixelFormat::PIXEL_BGRA32F:
                return 4;
            default:
                break;
        }

        return 0;
    }

    size_t PixelFormatUtils::PixelFormatDepth(PixelFormat f) {
        switch (f) {
            case PixelFormat::PIXEL_BGR8:
            case PixelFormat::PIXEL_RGB8:
                return sizeof(uint8_t) * 24;

            case PixelFormat::PIXEL_RGBA8:
            case PixelFormat::PIXEL_BGRA8:
                return sizeof(uint8_t) * 32;

            case PixelFormat::PIXEL_BGR32F:
            case PixelFormat::PIXEL_RGB32F:
                return sizeof(float) * 24;

            case PixelFormat::PIXEL_RGBA32F:
            case PixelFormat::PIXEL_BGRA32F:
                return sizeof(float) * 32;

            case PixelFormat::PIXEL_I420:
            case PixelFormat::PIXEL_NV12:
            case PixelFormat::PIXEL_NV21:
                return sizeof(uint8_t) * 12;

            case PixelFormat::PIXEL_YUYV:
            case PixelFormat::PIXEL_YVYU:
            case PixelFormat::PIXEL_UYVY:
                return sizeof(uint8_t) * 16;
            default:
                break;
        }

        return 0;
    }

    bool PixelFormatUtils::PixelFormatIsRGB(PixelFormat f) {
        if (f >= PixelFormat::PIXEL_RGB8 && f <= PixelFormat::PIXEL_RGBA32F)
            return true;

        return false;
    }

    bool PixelFormatUtils::PixelFormatIsBGR(PixelFormat f) {
        if (f >= PixelFormat::PIXEL_BGR8 && f <= PixelFormat::PIXEL_BGRA32F)
            return true;

        return false;
    }

    bool PixelFormatUtils::PixelFormatIsYUV(PixelFormat f) {
        if (f >= PixelFormat::PIXEL_YUYV && f <= PixelFormat::PIXEL_NV21)
            return true;

        return false;
    }

}  // namespace media
}  // namespace cosmo