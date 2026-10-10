#include "nn/utils/nv12_bt709_full_to_rgb.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace cosmo::nn {
namespace {

    bool Multiply(size_t left, size_t right, size_t& result) {
        if (right != 0 && left > std::numeric_limits<size_t>::max() / right)
            return false;
        result = left * right;
        return true;
    }

    bool Add(size_t left, size_t right, size_t& result) {
        if (left > std::numeric_limits<size_t>::max() - right)
            return false;
        result = left + right;
        return true;
    }

    uint8_t RoundedByte(int value) {
        // All intermediates fit int32 for 8-bit YUV. Clamp before shifting so
        // negative signed right shift is never required.
        return static_cast<uint8_t>(std::clamp(value + 32768, 0, 255 << 16) >> 16);
    }

}  // namespace

bool Nv12Bt709FullToRgb(const Nv12ImageView& source, const RgbImageView& destination, int offset_x,
                        int offset_y, const std::array<uint8_t, 3>& padding_rgb) noexcept {
    if (!source.data || !destination.data || source.width <= 0 || source.height <= 0 ||
        (source.width & 1) != 0 || (source.height & 1) != 0 ||
        source.row_stride < static_cast<size_t>(source.width) ||
        source.height_stride < static_cast<size_t>(source.height) || destination.width <= 0 ||
        destination.height <= 0 || offset_x < 0 || offset_y < 0 || source.width > destination.width ||
        source.height > destination.height || offset_x > destination.width - source.width ||
        offset_y > destination.height - source.height) {
        return false;
    }

    size_t uv_offset = 0, last_uv_row = 0, source_required = 0;
    size_t destination_row_bytes = 0, last_destination_row = 0, destination_required = 0;
    if (!Multiply(source.row_stride, source.height_stride, uv_offset) ||
        !Multiply(source.row_stride, static_cast<size_t>(source.height / 2 - 1), last_uv_row) ||
        !Add(uv_offset, last_uv_row, source_required) ||
        !Add(source_required, static_cast<size_t>(source.width), source_required) ||
        source_required > source.bytes ||
        !Multiply(static_cast<size_t>(destination.width), 3, destination_row_bytes) ||
        destination.row_stride < destination_row_bytes ||
        !Multiply(destination.row_stride, static_cast<size_t>(destination.height - 1),
                  last_destination_row) ||
        !Add(last_destination_row, destination_row_bytes, destination_required) ||
        destination_required > destination.bytes) {
        return false;
    }
    const auto source_address      = reinterpret_cast<uintptr_t>(source.data);
    const auto destination_address = reinterpret_cast<uintptr_t>(destination.data);
    if (source_required > std::numeric_limits<uintptr_t>::max() - source_address ||
        destination_required > std::numeric_limits<uintptr_t>::max() - destination_address ||
        (source_address < destination_address + destination_required &&
         destination_address < source_address + source_required)) {
        return false;
    }

    const bool uniform_padding = padding_rgb[0] == padding_rgb[1] && padding_rgb[0] == padding_rgb[2];
    for (int y = 0; y < destination.height; ++y) {
        auto* row = destination.data + static_cast<size_t>(y) * destination.row_stride;
        if (uniform_padding) {
            std::memset(row, padding_rgb[0], destination_row_bytes);
        } else {
            for (int x = 0; x < destination.width; ++x) {
                for (size_t channel = 0; channel < 3; ++channel)
                    row[static_cast<size_t>(x) * 3 + channel] = padding_rgb[channel];
            }
        }
    }
    for (int y = 0; y < source.height; ++y) {
        const auto* luma = source.data + static_cast<size_t>(y) * source.row_stride;
        const auto* uv   = source.data + uv_offset + static_cast<size_t>(y / 2) * source.row_stride;
        auto* rgb        = destination.data + static_cast<size_t>(y + offset_y) * destination.row_stride +
                    static_cast<size_t>(offset_x) * 3;
        for (int x = 0; x < source.width; ++x) {
            const int yy = static_cast<int>(luma[x]) << 16;
            const int cb = static_cast<int>(uv[x & ~1]) - 128;
            const int cr = static_cast<int>(uv[(x & ~1) + 1]) - 128;
            // BT.709 full-range coefficients, rounded to Q16:
            // R = Y + 1.5748 Cr; G = Y - 0.187324 Cb - 0.468124 Cr;
            // B = Y + 1.8556 Cb. Output order is explicitly RGB.
            rgb[static_cast<size_t>(x) * 3]     = RoundedByte(yy + 103206 * cr);
            rgb[static_cast<size_t>(x) * 3 + 1] = RoundedByte(yy - 12276 * cb - 30679 * cr);
            rgb[static_cast<size_t>(x) * 3 + 2] = RoundedByte(yy + 121609 * cb);
        }
    }
    return true;
}

}  // namespace cosmo::nn
