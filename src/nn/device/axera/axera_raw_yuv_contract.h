#pragma once

// Candidate contract for a raw 1920x1080 NV12 Y/UV two-input model. No such
// model, SDK behavior, color conversion, or runtime binding is established by
// this header. Actual compiled-model metadata must match before any use.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "media/NativeVideoBuffer.h"

namespace cosmo::nn::axera::raw_yuv {
enum class Error {
    None,
    InvalidSource,
    InvalidPlane,
    InvalidModelMetadata,
    InvalidIoLayout,
    InsufficientCapacity
};
// PitchedNchw is an exploratory graph layout where the padded row is part of
// the tensor shape and the graph itself discards padding. It is not evidence
// that a 1920x1080 model has been compiled or accepted on the device.
enum class Layout { LogicalNhwc, PitchedNchw };
enum class ColorProfile { Video709Limited, Jfif601Full };

struct Input {
    std::array<std::size_t, 4> shape{};
    std::size_t model_bytes{0};
    bool uint8_with_expected_layout{false};
    bool model_strides_present{false};
    std::array<std::size_t, 4> model_strides{};  // SDK metadata: elements.
    std::uint64_t io_physical{0};
    std::size_t io_bytes{0};
    bool io_strides_present{false};
    std::array<std::size_t, 4> io_strides{};  // SDK I/O: bytes; U8 elements are bytes.
};

// This checks only declarations and arithmetic. The caller must establish
// same-frame color provenance, model digest, device ownership, visibility,
// completion and that both SDK tensors actually consume the chosen planes.
inline Error Validate(const media::NativeVideoBuffer& source, const Input& y_input, const Input& uv_input,
                      std::size_t result_capacity, std::size_t required_capacity,
                      Layout layout      = Layout::LogicalNhwc,
                      ColorProfile color = ColorProfile::Video709Limited) {
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    if (!source.Valid() || source.memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        source.format != media::NativeVideoBufferFormat::NV12 || source.width != 1920 ||
        source.height != 1080 || source.crop_x || source.crop_y ||
        !((color == ColorProfile::Video709Limited &&
           source.color_space == media::NativeVideoColorSpace::Bt709 &&
           source.color_range == media::NativeVideoColorRange::Limited) ||
          (color == ColorProfile::Jfif601Full && source.color_space == media::NativeVideoColorSpace::Bt601 &&
           source.color_range == media::NativeVideoColorRange::Full)))
        return Error::InvalidSource;
    const auto& y  = source.planes[0];
    const auto& uv = source.planes[1];
    if (y.stride < 1920 || uv.stride < 1920 || y.stride > kMax / 1080 || uv.stride > kMax / 540 ||
        y.bytes < y.stride * 1080 || uv.bytes < uv.stride * 540 || y.bytes > source.bytes ||
        uv.bytes > source.bytes - y.bytes || y.bytes > kMax - y.physical_address ||
        uv.bytes > kMax - uv.physical_address ||
        (y.physical_address < uv.physical_address + uv.bytes &&
         uv.physical_address < y.physical_address + y.bytes))
        return Error::InvalidPlane;
    if (!y_input.uint8_with_expected_layout || !uv_input.uint8_with_expected_layout)
        return Error::InvalidModelMetadata;
    std::array<std::size_t, 4> y_shape{}, uv_shape{}, y_stride{}, uv_stride{};
    std::size_t y_model_bytes = 0, uv_model_bytes = 0;
    if (layout == Layout::LogicalNhwc) {
        y_shape        = {1, 1080, 1920, 1};
        uv_shape       = {1, 540, 960, 2};
        y_stride       = {y.stride * 1080, y.stride, 1, 1};
        uv_stride      = {uv.stride * 540, uv.stride, 2, 1};
        y_model_bytes  = 1920 * 1080;
        uv_model_bytes = 1920 * 540;
    } else if (layout == Layout::PitchedNchw) {
        y_shape        = {1, 1, 1080, y.stride};
        uv_shape       = {1, 1, 540, uv.stride};
        y_stride       = {y.stride * 1080, y.stride * 1080, y.stride, 1};
        uv_stride      = {uv.stride * 540, uv.stride * 540, uv.stride, 1};
        y_model_bytes  = y.stride * 1080;
        uv_model_bytes = uv.stride * 540;
    } else {
        return Error::InvalidModelMetadata;
    }
    if (y_input.shape != y_shape || uv_input.shape != uv_shape || y_input.model_bytes != y_model_bytes ||
        uv_input.model_bytes != uv_model_bytes)
        return Error::InvalidModelMetadata;
    if (!y_input.io_strides_present || !uv_input.io_strides_present || y_input.io_strides != y_stride ||
        uv_input.io_strides != uv_stride ||
        (y_input.model_strides_present && y_input.model_strides != y_stride) ||
        (uv_input.model_strides_present && uv_input.model_strides != uv_stride) ||
        y_input.io_physical != y.physical_address || uv_input.io_physical != uv.physical_address ||
        y_input.io_bytes < y.stride * 1080 || y_input.io_bytes > y.bytes ||
        uv_input.io_bytes < uv.stride * 540 || uv_input.io_bytes > uv.bytes)
        return Error::InvalidIoLayout;
    if (!required_capacity || result_capacity < required_capacity || result_capacity > 1000)
        return Error::InsufficientCapacity;
    return Error::None;
}
}  // namespace cosmo::nn::axera::raw_yuv
