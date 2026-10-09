#include "catch_amalgamated.hpp"
#include "infer/VlmEvaluationImage.h"
#include "infer/VlmEvaluationTokens.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_image_utils.h"

TEST_CASE("RK evaluation RGB matches the frozen Sophon reference tensor", "[vlm][rk][preprocess]") {
    constexpr int width  = 317;
    constexpr int height = 193;
    std::vector<uint8_t> rgb(width * height * 3), bgr(rgb.size());
    for (size_t i = 0; i < rgb.size(); ++i)
        rgb[i] = static_cast<uint8_t>(i % 256);
    for (size_t i = 0; i < rgb.size(); i += 3) {
        bgr[i]     = rgb[i + 2];
        bgr[i + 1] = rgb[i + 1];
        bgr[i + 2] = rgb[i];
    }
    const auto actual = cosmo::vlm_evaluation::ResizeRgb448(rgb.data(), rgb.size(), width, height, false);
    REQUIRE(actual == cosmo::vlm_evaluation::ResizeRgb448(bgr.data(), bgr.size(), width, height, true));
    cosmo::nn::qwen3vl::Config config;
    config.evaluation_square_448 = true;
    std::vector<float> reference;
    REQUIRE(
        cosmo::nn::qwen3vl::process_image_from_mat(bgr.data(), width, height, width * 3, config, reference));
    size_t mismatches = 0;
    for (int y = 0; y < 448; ++y) {
        for (int x = 0; x < 448; ++x) {
            const int patch = (((y / 32) * 14 + x / 32) * 2 + (y % 32) / 16) * 2 + (x % 32) / 16;
            for (int c = 0; c < 3; ++c) {
                const int offset  = patch * 1536 + c * 512 + (y % 16) * 16 + x % 16;
                const float value = actual[(y * 448 + x) * 3 + c] * (1.0F / 127.5F) - 1.0F;
                if (value != reference[offset])
                    ++mismatches;
            }
        }
    }
    REQUIRE(mismatches == 0);
}

TEST_CASE("RK evaluation rejects truncated image buffers", "[vlm][rk][preprocess]") {
    const uint8_t pixel[] = {255, 0, 0};
    REQUIRE_THROWS_AS(cosmo::vlm_evaluation::ResizeRgb448(pixel, 2, 1, 1, false), std::invalid_argument);
    REQUIRE_THROWS_AS(cosmo::vlm_evaluation::ResizeRgb448(nullptr, 3, 1, 1, false), std::invalid_argument);
    const auto actual = cosmo::vlm_evaluation::ResizeRgb448(pixel, 3, 1, 1, false);
    REQUIRE(actual.front() == 255);
    REQUIRE(actual.back() == 0);
}

TEST_CASE("RK evaluation accepts observed platform token counts within its budgets", "[vlm][rk][tokens]") {
    using cosmo::vlm_evaluation::TokenCountsWithinBudget;
    REQUIRE(TokenCountsWithinBudget(682, 64, 2048, 256));
    REQUIRE(TokenCountsWithinBudget(700, 64, 2048, 256));
    REQUIRE(TokenCountsWithinBudget(1792, 256, 2048, 256));
    REQUIRE_FALSE(TokenCountsWithinBudget(1793, 64, 2048, 256));
    REQUIRE_FALSE(TokenCountsWithinBudget(700, 257, 2048, 256));
    REQUIRE_FALSE(TokenCountsWithinBudget(0, 64, 2048, 256));
    REQUIRE_FALSE(TokenCountsWithinBudget(-1, 64, 2048, 256));
    REQUIRE_FALSE(TokenCountsWithinBudget(700, -1, 2048, 256));
}

TEST_CASE("RK callback counts detect output budget independently of SDK perf convention",
          "[vlm][rk][tokens]") {
    using namespace cosmo::vlm_evaluation;
    REQUIRE(CallbackOutputWithinBudget(0, 256));
    REQUIRE_FALSE(OutputBudgetReached(0, 256));
    REQUIRE(CallbackOutputWithinBudget(1, 256));
    REQUIRE_FALSE(OutputBudgetReached(1, 256));
    REQUIRE(OutputBudgetReached(1, 1));
    // Observed SDK count is one less than the NORMAL callback count.
    REQUIRE(TokenCountsWithinBudget(700, 255, 2048, 256));
    REQUIRE(CallbackOutputWithinBudget(256, 256));
    REQUIRE(OutputBudgetReached(256, 256));
    // An SDK count alone must not allow an actual callback stream over budget.
    REQUIRE(TokenCountsWithinBudget(700, 256, 2048, 256));
    REQUIRE_FALSE(CallbackOutputWithinBudget(257, 256));
    REQUIRE_FALSE(CallbackOutputWithinBudget(0, 0));
    REQUIRE_FALSE(OutputBudgetReached(0, 0));
}
