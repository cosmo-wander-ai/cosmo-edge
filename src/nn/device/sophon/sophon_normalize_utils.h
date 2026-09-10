#pragma once

#include <array>

#include "nn/utils/image_format_utils.h"

namespace cosmo::nn {

inline constexpr bool SophonNormalizeNeedsRedBlueSwap(ImageFormat input_format, bool model_is_bgr) {
    const bool input_is_bgr = input_format == IMAGE_BGR || input_format == IMAGE_BGRA;
    const bool input_is_rgb = input_format == IMAGE_RGB || input_format == IMAGE_RGBA;
    return (input_is_bgr && !model_is_bgr) || (input_is_rgb && model_is_bgr);
}

template <typename T>
inline constexpr std::array<T, 3> SophonNormalizeSourceChannelOrder(
    const std::array<T, 3>& model_channel_values, bool swap_red_blue) {
    return swap_red_blue
               ? std::array<T, 3>{model_channel_values[2], model_channel_values[1], model_channel_values[0]}
               : model_channel_values;
}

}  // namespace cosmo::nn
