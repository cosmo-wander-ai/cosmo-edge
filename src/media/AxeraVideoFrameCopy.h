#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cosmo::media {

/// Copy a mapped, potentially padded NV12/NV21 frame into compact I420.
/// The two source planes can have independent addresses and strides. All
/// extents are validated before writing, so a rejected frame leaves dst alone.
inline bool CopyAxeraSemiplanarToI420(const uint8_t* y, size_t y_bytes, size_t y_stride, const uint8_t* uv,
                                      size_t uv_bytes, size_t uv_stride, size_t width, size_t height,
                                      bool chroma_vu, uint8_t* dst, size_t dst_bytes) {
    if (!y || !uv || !dst || width == 0 || height == 0 || (width & 1U) || (height & 1U) || y_stride < width ||
        uv_stride < width) {
        return false;
    }
    const auto max        = std::numeric_limits<size_t>::max();
    const auto fits_plane = [max, width](size_t stride, size_t rows, size_t bytes) {
        return rows > 0 && (rows - 1) <= (max - width) / stride && (rows - 1) * stride + width <= bytes;
    };
    if (!fits_plane(y_stride, height, y_bytes) || !fits_plane(uv_stride, height / 2, uv_bytes) ||
        width > max / height) {
        return false;
    }
    const size_t y_size = width * height;
    if (y_size > max - y_size / 2 || dst_bytes < y_size + y_size / 2) {
        return false;
    }
    auto* dst_u = dst + y_size;
    auto* dst_v = dst_u + y_size / 4;
    for (size_t row = 0; row < height; ++row) {
        std::memcpy(dst + row * width, y + row * y_stride, width);
    }
    for (size_t row = 0; row < height / 2; ++row) {
        const auto* in = uv + row * uv_stride;
        for (size_t column = 0; column < width / 2; ++column) {
            dst_u[row * (width / 2) + column] = in[column * 2 + (chroma_vu ? 1 : 0)];
            dst_v[row * (width / 2) + column] = in[column * 2 + (chroma_vu ? 0 : 1)];
        }
    }
    return true;
}

/// Prepare an encoder-owned NV12 buffer from compact I420. Destination planes
/// may be independently mapped and padded; fill padding with neutral black.
inline bool CopyI420ToAxeraNv12(const uint8_t* src, size_t src_bytes, size_t width, size_t height, uint8_t* y,
                                size_t y_bytes, size_t y_stride, uint8_t* uv, size_t uv_bytes,
                                size_t uv_stride) {
    if (!src || !y || !uv || width == 0 || height == 0 || (width & 1U) || (height & 1U) || y_stride < width ||
        uv_stride < width) {
        return false;
    }
    const auto max = std::numeric_limits<size_t>::max();
    if (width > max / height || y_stride > max / height || uv_stride > max / (height / 2)) {
        return false;
    }
    const size_t pixels = width * height;
    if (pixels > max - pixels / 2 || src_bytes < pixels + pixels / 2 || y_bytes < y_stride * height ||
        uv_bytes < uv_stride * (height / 2)) {
        return false;
    }
    std::memset(y, 16, y_stride * height);
    std::memset(uv, 128, uv_stride * (height / 2));
    const auto* u = src + pixels;
    const auto* v = u + pixels / 4;
    for (size_t row = 0; row < height; ++row) {
        std::memcpy(y + row * y_stride, src + row * width, width);
    }
    for (size_t row = 0; row < height / 2; ++row) {
        for (size_t column = 0; column < width / 2; ++column) {
            uv[row * uv_stride + column * 2]     = u[row * (width / 2) + column];
            uv[row * uv_stride + column * 2 + 1] = v[row * (width / 2) + column];
        }
    }
    return true;
}

}  // namespace cosmo::media
