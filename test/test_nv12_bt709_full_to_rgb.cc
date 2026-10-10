#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/utils/nv12_bt709_full_to_rgb.h"

namespace {

using cosmo::nn::Nv12Bt709FullToRgb;
using cosmo::nn::Nv12ImageView;
using cosmo::nn::RgbImageView;

std::array<uint8_t, 3> Reference709Full(int y, int u, int v) {
    // Independent double-precision derivation from BT.709 Kr/Kb, including
    // full-range luma and RGB saturation, rather than the helper's Q16 math.
    constexpr double kr = 0.2126, kb = 0.0722, kg = 1.0 - kr - kb;
    const double cb = u - 128, cr = v - 128;
    const auto byte = [](double value) {
        return static_cast<uint8_t>(std::clamp<long>(std::lround(value), 0, 255));
    };
    return {byte(y + 2 * (1 - kr) * cr), byte(y - 2 * kb * (1 - kb) / kg * cb - 2 * kr * (1 - kr) / kg * cr),
            byte(y + 2 * (1 - kb) * cb)};
}

}  // namespace

TEST_CASE("NV12 full BT709 preserves the complete neutral luma range", "[nn][nv12-bt709]") {
    const std::array<uint8_t, 6> input{0, 16, 235, 255, 128, 128};
    std::array<uint8_t, 12> output{};
    REQUIRE(Nv12Bt709FullToRgb({input.data(), input.size(), 2, 2, 2, 2},
                               {output.data(), output.size(), 2, 2, 6}, 0, 0, {114, 114, 114}));
    CHECK(output == std::array<uint8_t, 12>{0, 0, 0, 16, 16, 16, 235, 235, 235, 255, 255, 255});
}

TEST_CASE("NV12 full BT709 agrees with an independent color reference", "[nn][nv12-bt709]") {
    constexpr int width = 512, height = 2;
    std::vector<uint8_t> input(width * height * 3 / 2), output(width * height * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            input[y * width + x] = static_cast<uint8_t>(x / 2);
    int max_error           = 0;
    size_t checked_channels = 0;
    // All 256 luma levels crossed with chroma samples across the entire range,
    // explicitly including both extremes and values around neutral chroma.
    constexpr std::array<int, 17> chroma{0,   1,   16,  32,  48,  64,  96,  127, 128,
                                         129, 160, 192, 208, 224, 240, 254, 255};
    for (int u : chroma) {
        for (int v : chroma) {
            for (int x = 0; x < width; x += 2) {
                input[width * height + x]     = static_cast<uint8_t>(u);
                input[width * height + x + 1] = static_cast<uint8_t>(v);
            }
            REQUIRE(Nv12Bt709FullToRgb({input.data(), input.size(), width, height, width, height},
                                       {output.data(), output.size(), width, height, width * 3}, 0, 0,
                                       {114, 114, 114}));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const auto expected = Reference709Full(x / 2, u, v);
                    for (size_t channel = 0; channel < 3; ++channel) {
                        max_error = std::max(
                            max_error, std::abs(static_cast<int>(output[(y * width + x) * 3 + channel]) -
                                                expected[channel]));
                        ++checked_channels;
                    }
                }
            }
        }
    }
    CHECK(checked_channels == chroma.size() * chroma.size() * width * height * 3);
    CHECK(max_error <= 1);
}

TEST_CASE("NV12 full BT709 fills uniform letterbox pixels exactly", "[nn][nv12-bt709]") {
    const std::array<uint8_t, 6> input{0, 16, 235, 255, 128, 128};
    std::array<uint8_t, 48> output{};
    REQUIRE(Nv12Bt709FullToRgb({input.data(), input.size(), 2, 2, 2, 2},
                               {output.data(), output.size(), 4, 4, 12}, 1, 1, {114, 114, 114}));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const auto expected = x >= 1 && x < 3 && y >= 1 && y < 3 ? input[(y - 1) * 2 + x - 1] : 114;
            for (int channel = 0; channel < 3; ++channel)
                CHECK(output[(y * 4 + x) * 3 + channel] == expected);
        }
    }
}

TEST_CASE("NV12 full BT709 respects padded planes and RGB ROI canaries", "[nn][nv12-bt709]") {
    constexpr size_t guard = 19, source_stride = 10, source_hstride = 8, destination_stride = 40;
    constexpr int width = 6, height = 4, destination_width = 11, destination_height = 9;
    constexpr int offset_x = 2, offset_y = 3;
    constexpr size_t source_bytes = source_stride * source_hstride + source_stride + width;
    constexpr size_t destination_bytes =
        (destination_height - 1) * destination_stride + destination_width * 3;
    std::vector<uint8_t> input(guard + source_bytes + guard, 0xa5);
    std::vector<uint8_t> output(guard + destination_bytes + guard, 0xcc);
    auto* source      = input.data() + guard;
    auto* destination = output.data() + guard;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            source[y * source_stride + x] = static_cast<uint8_t>(20 + y * 30 + x * 9);
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width; x += 2) {
            source[source_stride * source_hstride + y * source_stride + x] =
                static_cast<uint8_t>(40 + x * 23 + y * 31);
            source[source_stride * source_hstride + y * source_stride + x + 1] =
                static_cast<uint8_t>(210 - x * 19 - y * 29);
        }
    }
    const auto original_input = input;
    const std::array<uint8_t, 3> padding{7, 11, 13};
    REQUIRE(Nv12Bt709FullToRgb(
        {source, source_bytes, width, height, source_stride, source_hstride},
        {destination, destination_bytes, destination_width, destination_height, destination_stride}, offset_x,
        offset_y, padding));
    CHECK(input == original_input);
    for (size_t i = 0; i < guard; ++i) {
        CHECK(output[i] == 0xcc);
        CHECK(output[guard + destination_bytes + i] == 0xcc);
    }
    for (int y = 0; y < destination_height; ++y) {
        for (int x = 0; x < destination_width; ++x) {
            auto expected = padding;
            const bool in_roi =
                x >= offset_x && x < offset_x + width && y >= offset_y && y < offset_y + height;
            if (in_roi) {
                const int sx = x - offset_x, sy = y - offset_y;
                const auto* uv = source + source_stride * source_hstride + (sy / 2) * source_stride;
                expected = Reference709Full(source[sy * source_stride + sx], uv[sx & ~1], uv[(sx & ~1) + 1]);
            }
            for (size_t channel = 0; channel < 3; ++channel) {
                const auto actual = destination[y * destination_stride + x * 3 + channel];
                if (in_roi)
                    CHECK(std::abs(static_cast<int>(actual) - expected[channel]) <= 1);
                else
                    CHECK(actual == expected[channel]);
            }
        }
        if (y + 1 < destination_height)
            for (size_t x = destination_width * 3; x < destination_stride; ++x)
                CHECK(destination[y * destination_stride + x] == 0xcc);
    }
}

TEST_CASE("NV12 full BT709 rejects invalid views without writing", "[nn][nv12-bt709]") {
    std::vector<uint8_t> input(96, 128), output(96, 0xcc);
    const auto original_input = input, original_output = output;
    Nv12ImageView source{input.data(), 6, 2, 2, 2, 2};
    RgbImageView destination{output.data(), 48, 4, 4, 12};
    int x = 1, y = 1;

    SECTION("missing source") {
        source.data = nullptr;
    }
    SECTION("missing destination") {
        destination.data = nullptr;
    }
    SECTION("zero width") {
        source.width = 0;
    }
    SECTION("negative height") {
        source.height = -2;
    }
    SECTION("odd width") {
        source.width = 1;
    }
    SECTION("odd height") {
        source.height = 1;
    }
    SECTION("short luma stride") {
        source.row_stride = 1;
    }
    SECTION("short luma height stride") {
        source.height_stride = 1;
    }
    SECTION("short chroma capacity") {
        source.bytes = 5;
    }
    SECTION("source plane multiplication overflow") {
        source.height_stride = std::numeric_limits<size_t>::max();
    }
    SECTION("source range addition overflow") {
        source.row_stride = std::numeric_limits<size_t>::max() / 2;
        source.bytes      = std::numeric_limits<size_t>::max();
    }
    SECTION("zero destination width") {
        destination.width = 0;
    }
    SECTION("negative destination height") {
        destination.height = -1;
    }
    SECTION("source wider than destination") {
        destination.width = 1;
    }
    SECTION("source taller than destination") {
        destination.height = 1;
    }
    SECTION("negative x offset") {
        x = -1;
    }
    SECTION("negative y offset") {
        y = -1;
    }
    SECTION("x ROI overflow") {
        x = std::numeric_limits<int>::max();
    }
    SECTION("y ROI overflow") {
        y = std::numeric_limits<int>::max();
    }
    SECTION("short destination row") {
        destination.row_stride = 11;
    }
    SECTION("short destination capacity") {
        destination.bytes = 47;
    }
    SECTION("destination multiplication overflow") {
        destination.row_stride = std::numeric_limits<size_t>::max();
        destination.bytes      = std::numeric_limits<size_t>::max();
    }
    SECTION("destination range addition overflow") {
        destination.row_stride = std::numeric_limits<size_t>::max() / 3;
        destination.bytes      = std::numeric_limits<size_t>::max();
    }
    SECTION("overlapping buffers") {
        destination.data = input.data();
    }
    SECTION("partially overlapping buffers") {
        destination.data = input.data() + 3;
    }
    SECTION("wrapped source address") {
        source.data = reinterpret_cast<const uint8_t*>(std::numeric_limits<uintptr_t>::max() - 3);
    }
    SECTION("wrapped destination address") {
        destination.data = reinterpret_cast<uint8_t*>(std::numeric_limits<uintptr_t>::max() - 3);
    }

    CHECK_FALSE(Nv12Bt709FullToRgb(source, destination, x, y, {114, 114, 114}));
    CHECK(input == original_input);
    CHECK(output == original_output);
}
