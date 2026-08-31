#include "catch_amalgamated.hpp"

#include <array>

#include "nn/device/sophon/sophon_normalize_utils.h"

TEST_CASE("Sophon normalize maps image and coefficient channel order", "[nn][sophon][normalize]") {
    using cosmo::nn::IMAGE_BGR;
    using cosmo::nn::IMAGE_BGRA;
    using cosmo::nn::IMAGE_GRAY;
    using cosmo::nn::IMAGE_RGB;
    using cosmo::nn::IMAGE_RGBA;
    using cosmo::nn::SophonNormalizeNeedsRedBlueSwap;
    using cosmo::nn::SophonNormalizeSourceChannelOrder;

    CHECK_FALSE(SophonNormalizeNeedsRedBlueSwap(IMAGE_BGR, true));
    CHECK(SophonNormalizeNeedsRedBlueSwap(IMAGE_BGR, false));
    CHECK(SophonNormalizeNeedsRedBlueSwap(IMAGE_RGB, true));
    CHECK_FALSE(SophonNormalizeNeedsRedBlueSwap(IMAGE_RGB, false));
    CHECK_FALSE(SophonNormalizeNeedsRedBlueSwap(IMAGE_BGRA, true));
    CHECK(SophonNormalizeNeedsRedBlueSwap(IMAGE_BGRA, false));
    CHECK(SophonNormalizeNeedsRedBlueSwap(IMAGE_RGBA, true));
    CHECK_FALSE(SophonNormalizeNeedsRedBlueSwap(IMAGE_RGBA, false));
    CHECK_FALSE(SophonNormalizeNeedsRedBlueSwap(IMAGE_GRAY, true));
    CHECK_FALSE(SophonNormalizeNeedsRedBlueSwap(IMAGE_GRAY, false));

    const std::array<float, 3> model_channel_values{1.0f, 2.0f, 3.0f};
    CHECK(SophonNormalizeSourceChannelOrder(model_channel_values, false) == model_channel_values);
    CHECK(SophonNormalizeSourceChannelOrder(model_channel_values, true) ==
          std::array<float, 3>{3.0f, 2.0f, 1.0f});
}
