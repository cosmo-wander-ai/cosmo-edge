#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cosmo::nn {

struct Nv12ImageView {
    const uint8_t* data{nullptr};
    size_t bytes{0};
    int width{0};
    int height{0};
    size_t row_stride{0};
    // The interleaved UV plane starts at row_stride * height_stride.
    size_t height_stride{0};
};

struct RgbImageView {
    uint8_t* data{nullptr};
    size_t bytes{0};
    int width{0};
    int height{0};
    size_t row_stride{0};
};

// Convert full-range BT.709 NV12 into an RGB ROI without resizing. Other visible
// destination pixels receive padding_rgb; row padding bytes remain untouched.
// Invalid dimensions, capacities, ROI or overlapping buffers return false before
// writing any output. Source width and height must both be positive and even.
bool Nv12Bt709FullToRgb(const Nv12ImageView& source, const RgbImageView& destination, int offset_x,
                        int offset_y, const std::array<uint8_t, 3>& padding_rgb) noexcept;

}  // namespace cosmo::nn
