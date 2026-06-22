#pragma once

#include <strings.h>

namespace cosmo::nn {

typedef enum {
    // RGB
    IMAGE_RGB = 0,
    IMAGE_RGBA,

    // BGR
    IMAGE_BGR,
    IMAGE_BGRA,

    // YUV
    IMAGE_YUYV, /**< YUYV 4:2:2 packed (`'yuyv'`) */
    IMAGE_YVYU, /**< YVYU 4:2:2 packed (`'yvyu'`) */
    IMAGE_UYVY, /**< UYVY 4:2:2 packed (`'uyvy'`) */
    IMAGE_I420, /**< I420 4:2:0 planar (`'i420'`) */
    IMAGE_YV12, /**< YV12 4:2:0 planar (`'yv12'`) */
    IMAGE_NV12, /**< NV12 4:2:0 planar (`'nv12'`) */

    // gray
    IMAGE_GRAY,

    // extra
    IMAGE_FORMAT_COUNT,
    IMAGE_UNKNOWN = 999,
    IMAGE_DEFAULT = IMAGE_RGB,
} ImageFormat;

inline const char* ImageFormatToStr(ImageFormat format) {
    switch (format) {
        case IMAGE_RGB:
            return "rgb";
        case IMAGE_RGBA:
            return "rgba";
        case IMAGE_BGR:
            return "bgr";
        case IMAGE_BGRA:
            return "bgra";
        case IMAGE_YUYV:
            return "yuyv";
        case IMAGE_YVYU:
            return "yvyu";
        case IMAGE_UYVY:
            return "uyvy";
        case IMAGE_I420:
            return "i420";
        case IMAGE_YV12:
            return "yv12";
        case IMAGE_NV12:
            return "nv12";
        case IMAGE_GRAY:
            return "gray";
        case IMAGE_UNKNOWN:
        default:
            return "unknown";
    }

    return "unknown";
}

inline ImageFormat ImageFormatFromStr(const char* str) {
    if (!str)
        return IMAGE_UNKNOWN;

    for (int i = 0; i < IMAGE_FORMAT_COUNT; ++i) {
        const ImageFormat fmt = static_cast<ImageFormat>(i);

        if (strcasecmp(str, ImageFormatToStr(fmt)) == 0) {
            return fmt;
        }
    }

    return IMAGE_UNKNOWN;
}

inline size_t ImageFormatChannels(ImageFormat format) {
    switch (format) {
        case IMAGE_RGB:
        case IMAGE_BGR:
            return 3;

        case IMAGE_RGBA:
        case IMAGE_BGRA:
            return 4;

        case IMAGE_GRAY:
            return 1;

        case IMAGE_YUYV:
        case IMAGE_YVYU:
        case IMAGE_UYVY:
        case IMAGE_I420:
        case IMAGE_YV12:
        case IMAGE_NV12:
            return 3;
        default:
            return 0;
    }

    return 0;
}

inline size_t ImageFormatDepth(ImageFormat format) {
    switch (format) {
        case IMAGE_RGB:
        case IMAGE_BGR:
            return 24;

        case IMAGE_RGBA:
        case IMAGE_BGRA:
            return 32;

        case IMAGE_GRAY:
            return 8;

        case IMAGE_YUYV:
        case IMAGE_YVYU:
        case IMAGE_UYVY:
            return 16;
        case IMAGE_I420:
        case IMAGE_YV12:
        case IMAGE_NV12:
            return 12;
        default:
            return 0;
    }

    return 0;
}

inline size_t ImageFormatSize(ImageFormat format, size_t width, size_t height) {
    return (width * height * ImageFormatDepth(format)) / 8;
}

inline bool ImageFormatIsRGB(ImageFormat format) {
    return (format == IMAGE_RGB || format == IMAGE_RGBA);
}

inline bool ImageFormatIsBGR(ImageFormat format) {
    return (format == IMAGE_BGR || format == IMAGE_BGRA);
}

inline bool ImageFormatIsYUV(ImageFormat format) {
    return (format >= IMAGE_YUYV && format <= IMAGE_NV12);
}

inline bool ImageFormatIsGray(ImageFormat format) {
    return (format == IMAGE_GRAY);
}

}  // namespace cosmo::nn
