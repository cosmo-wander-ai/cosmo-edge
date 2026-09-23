#include "catch_amalgamated.hpp"
#include "nn/device/sophon/qwen3vl/qwen3vl_image_utils.h"
#include "nn/device/sophon/qwen3vl/qwen3vl_predict_utils.h"

#ifndef COSMO_NN_USE_SOPHON_BACKEND
#include "nn/device/sophon/qwen3vl/qwen3vl_image_utils.cc"
#include "nn/device/sophon/qwen3vl/qwen3vl_predict_utils.cc"
#endif

TEST_CASE("VLM evaluation matches template trailing trim without changing business prompts",
          "[vlm][prompt]") {
    const std::vector<std::vector<int>> grid = {{1, 2, 2}};
    const std::string prefix   = "<|im_start|>user\n<|vision_start|><|image_pad|><|vision_end|>";
    const std::string suffix   = "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
    const std::string question = "  Describe this image.\n\t ";
    REQUIRE(cosmo::nn::qwen3vl::BuildImagePrompt(question, grid, true, true) ==
            prefix + "  Describe this image." + suffix);
    REQUIRE(cosmo::nn::qwen3vl::BuildImagePrompt(question, grid, true) == prefix + question + suffix);
    REQUIRE(cosmo::nn::qwen3vl::BuildImagePrompt("\n\t", grid, true, true) == prefix + suffix);
}

TEST_CASE("VLM evaluation preserves RGB order and fixed patch geometry", "[vlm][preprocess]") {
    cosmo::nn::qwen3vl::Config config;
    config.evaluation_square_448 = true;
    const unsigned char bgr[]    = {0, 0, 255};
    std::vector<float> values;
    REQUIRE(cosmo::nn::qwen3vl::process_image_from_mat(bgr, 1, 1, 3, config, values));
    REQUIRE(config.grid_thw == std::vector<int>{1, 28, 28});
    REQUIRE(values.size() == 784 * 1536);
    // Each patch stores C,T,H,W; both temporal copies have red=1, green=blue=-1.
    for (size_t i = 0; i < values.size(); ++i) {
        const auto channel = (i % 1536) / 512;
        REQUIRE(values[i] == (channel == 0 ? 1.0F : -1.0F));
    }
}

TEST_CASE("VLM evaluation pads odd remainders on bottom and right", "[vlm][preprocess]") {
    cosmo::nn::qwen3vl::Config config;
    config.evaluation_square_448 = true;
    const unsigned char bgr[]    = {0, 0, 255, 0, 0, 255};
    std::vector<float> values;
    REQUIRE(cosmo::nn::qwen3vl::process_image_from_mat(bgr, 2, 1, 6, config, values));
    const float padding = 128.0F * (1.0F / 127.5F) - 1.0F;
    REQUIRE(values.front() == 1.0F);
    REQUIRE(values.back() == padding);
}

TEST_CASE("VLM evaluation bilinear interpolation rounds to uint8 before normalization", "[vlm][preprocess]") {
    cosmo::nn::qwen3vl::Config config;
    config.evaluation_square_448 = true;
    const unsigned char bgr[]    = {0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255};
    std::vector<float> values;
    REQUIRE(cosmo::nn::qwen3vl::process_image_from_mat(bgr, 2, 2, 6, config, values));
    const int x = 223, y = 223;
    const int patch  = (((y / 32) * 14 + x / 32) * 2 + (y % 32) / 16) * 2 + (x % 32) / 16;
    const int offset = patch * 1536 + (y % 16) * 16 + x % 16;
    REQUIRE(values[offset] == 128.0F * (1.0F / 127.5F) - 1.0F);
    REQUIRE(values[offset + 512] == 127.0F * (1.0F / 127.5F) - 1.0F);
    REQUIRE(values[offset + 1024] == 127.0F * (1.0F / 127.5F) - 1.0F);
}
