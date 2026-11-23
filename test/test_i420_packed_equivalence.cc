#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/I420PackedCpu.h"

namespace {
std::vector<uint8_t> Reference(const std::vector<uint8_t>& input, int width, int height, bool bgr) {
    const auto pixels = static_cast<size_t>(width) * height;
    const auto* y     = input.data();
    const auto* u     = y + pixels;
    const auto* v     = u + pixels / 4;
    std::vector<uint8_t> output(pixels * 3);
    const auto clip = [](int value) { return static_cast<uint8_t>(std::clamp(value, 0, 255)); };
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const int c        = y[row * width + col] - 16;
            const int d        = u[(row / 2) * (width / 2) + col / 2] - 128;
            const int e        = v[(row / 2) * (width / 2) + col / 2] - 128;
            const auto red     = clip((298 * c + 409 * e + 128) >> 8);
            const auto green   = clip((298 * c - 100 * d - 208 * e + 128) >> 8);
            const auto blue    = clip((298 * c + 516 * d + 128) >> 8);
            const auto offset  = (static_cast<size_t>(row) * width + col) * 3;
            output[offset]     = bgr ? blue : red;
            output[offset + 1] = green;
            output[offset + 2] = bgr ? red : blue;
        }
    }
    return output;
}
}  // namespace

TEST_CASE("I420 packed conversion preserves legacy CPU colors", "[media][i420][equivalence]") {
    std::mt19937 random(3588);
    for (const auto dimensions : {std::array<int, 2>{2, 2}, {8, 4}, {54, 142}, {640, 360}, {1920, 1080}}) {
        const int width   = dimensions[0];
        const int height  = dimensions[1];
        const auto pixels = static_cast<size_t>(width) * height;
        std::vector<uint8_t> input(pixels * 3 / 2), actual(pixels * 3);
        for (auto& value : input)
            value = static_cast<uint8_t>(random());
        for (bool bgr : {false, true}) {
            const auto expected = Reference(input, width, height, bgr);
            const bool ok =
                bgr ? cosmo::media::detail::I420ToPacked<true>(input.data(), actual.data(), width, height)
                    : cosmo::media::detail::I420ToPacked<false>(input.data(), actual.data(), width, height);
            REQUIRE(ok);
            REQUIRE(actual == expected);
        }
    }
}

TEST_CASE("I420 packed conversion rejects invalid dimensions and pointers", "[media][i420][equivalence]") {
    std::array<uint8_t, 12> bytes{};
    REQUIRE_FALSE(cosmo::media::detail::I420ToPacked<true>(bytes.data(), bytes.data(), 1, 2));
    REQUIRE_FALSE(cosmo::media::detail::I420ToPacked<false>(bytes.data(), bytes.data(), 2, 1));
    REQUIRE_FALSE(cosmo::media::detail::I420ToPacked<true>(nullptr, bytes.data(), 2, 2));
    REQUIRE_FALSE(cosmo::media::detail::I420ToPacked<true>(bytes.data(), nullptr, 2, 2));
}
