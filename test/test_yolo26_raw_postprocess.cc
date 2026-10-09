#include <array>
#include <limits>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/utils/yolo26_raw_postprocess.h"

using cosmo::nn::DecodeYolo26RawHead;

namespace {

std::vector<float> ChannelMajor(const std::vector<std::vector<float>>& rows) {
    const std::size_t count = rows.size();
    const std::size_t cols  = rows.front().size();
    std::vector<float> result(count * cols);
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(rows[i].size() == cols);
        for (std::size_t c = 0; c < cols; ++c)
            result[c * count + i] = rows[i][c];
    }
    return result;
}

TEST_CASE("YOLO26 raw helper supports one, two and eighty classes", "[nn][yolo26][postprocess]") {
    const int classes = GENERATE(1, 2, 80);
    std::vector<float> row(4 + classes, 0.05f);
    row[0]            = 100.f;
    row[1]            = 50.f;
    row[2]            = 40.f;
    row[3]            = 20.f;
    row[3 + classes]  = 0.9f;
    const auto output = DecodeYolo26RawHead(row.data(), 1, row.size(), 0.25f, 0.7f, 10);
    REQUIRE(output.size() == 1);
    CHECK(output[0].class_id == classes - 1);
    CHECK(output[0].confidence == Catch::Approx(0.9f));
    CHECK(output[0].cx == 100.f);
    CHECK(output[0].cy == 50.f);
    CHECK(output[0].width == 40.f);
    CHECK(output[0].height == 20.f);
}

TEST_CASE("YOLO26 raw helper applies class-aware NMS and strict confidence", "[nn][yolo26][postprocess]") {
    const auto raw    = ChannelMajor({
        {100, 100, 40, 40, 0.90f, 0.10f},
        {101, 100, 40, 40, 0.80f, 0.05f},
        {100, 100, 40, 40, 0.20f, 0.85f},
        {220, 100, 30, 20, 0.70f, 0.10f},
        {300, 100, 20, 20, 0.25f, 0.10f},
    });
    const auto output = DecodeYolo26RawHead(raw.data(), 5, 6, 0.25f, 0.7f, 10);
    REQUIRE(output.size() == 3);
    CHECK(output[0].class_id == 0);
    CHECK(output[0].confidence == Catch::Approx(0.9f));
    CHECK(output[1].class_id == 1);
    CHECK(output[1].confidence == Catch::Approx(0.85f));
    CHECK(output[2].cx == 220.f);
}

TEST_CASE("YOLO26 raw helper preserves equality at the IoU boundary and truncates after NMS",
          "[nn][yolo26][postprocess]") {
    const auto raw = ChannelMajor({
        {0, 0, 8, 8, 0.9f},
        {2, 0, 4, 8, 0.8f},  // IoU exactly 0.5.
        {20, 0, 4, 4, 0.7f},
    });
    CHECK(DecodeYolo26RawHead(raw.data(), 3, 5, 0.1f, 0.5f, 10).size() == 3);
    CHECK(DecodeYolo26RawHead(raw.data(), 3, 5, 0.1f, 0.49f, 10).size() == 2);
    const auto limited = DecodeYolo26RawHead(raw.data(), 3, 5, 0.1f, 0.49f, 2);
    REQUIRE(limited.size() == 2);
    CHECK(limited[0].cx == 0.f);
    CHECK(limited[1].cx == 20.f);
    CHECK(DecodeYolo26RawHead(raw.data(), 3, 5, 0.1f, 0.5f, 1).size() == 1);
}

TEST_CASE("YOLO26 raw helper resolves score and class ties deterministically", "[nn][yolo26][postprocess]") {
    const auto raw = ChannelMajor({
        {100, 100, 40, 40, 0.9f, 0.9f},
        {101, 100, 40, 40, 0.9f, 0.1f},
        {300, 100, 40, 40, 0.9f, 0.1f},
        {500, 100, 40, 40, 0.9f, 0.1f},
    });
    for (int repeat = 0; repeat < 3; ++repeat) {
        const auto output = DecodeYolo26RawHead(raw.data(), 4, 6, 0.1f, 0.7f, 3);
        REQUIRE(output.size() == 3);
        CHECK(output[0].class_id == 0);
        CHECK(output[0].cx == 100.f);
        CHECK(output[1].cx == 300.f);
        CHECK(output[2].cx == 500.f);
    }
}

TEST_CASE("YOLO26 raw helper rejects invalid geometry and keeps valid candidates",
          "[nn][yolo26][postprocess]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int field = 0; field < 4; ++field) {
        for (float invalid : {nan, inf, -inf}) {
            std::vector<std::vector<float>> rows{{10, 10, 20, 20, 0.9f}, {40, 40, 20, 20, 0.8f}};
            rows[0][field]    = invalid;
            const auto raw    = ChannelMajor(rows);
            const auto output = DecodeYolo26RawHead(raw.data(), 2, 5, 0.25f, 0.7f, 10);
            REQUIRE(output.size() == 1);
            CHECK(output[0].cx == 40.f);
        }
    }
    for (int field : {2, 3}) {
        for (float invalid : {0.f, -1.f}) {
            std::array<float, 5> row{10, 10, 20, 20, 0.9f};
            row[field] = invalid;
            CHECK(DecodeYolo26RawHead(row.data(), 1, 5, 0.25f, 0.7f, 10).empty());
        }
    }
}

TEST_CASE("YOLO26 raw helper ignores unusable class scores", "[nn][yolo26][postprocess]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float invalid : {nan, inf, -inf, -0.1f, 1.1f}) {
        const std::array<float, 6> row{10, 10, 20, 20, invalid, 0.8f};
        const auto output = DecodeYolo26RawHead(row.data(), 1, 6, 0.25f, 0.7f, 10);
        REQUIRE(output.size() == 1);
        CHECK(output[0].class_id == 1);
        const std::array<float, 5> only_invalid{10, 10, 20, 20, invalid};
        CHECK(DecodeYolo26RawHead(only_invalid.data(), 1, 5, 0.25f, 0.7f, 10).empty());
    }
}

TEST_CASE("YOLO26 raw helper rejects invalid dimensions and thresholds", "[nn][yolo26][postprocess]") {
    const std::array<float, 5> row{10, 10, 20, 20, 0.9f};
    CHECK(DecodeYolo26RawHead(nullptr, 1, 5, 0.25f, 0.7f, 10).empty());
    CHECK(DecodeYolo26RawHead(row.data(), 0, 5, 0.25f, 0.7f, 10).empty());
    CHECK(DecodeYolo26RawHead(row.data(), 1, 4, 0.25f, 0.7f, 10).empty());
    CHECK(DecodeYolo26RawHead(row.data(), 1, 5, 0.25f, 0.7f, 0).empty());
    for (float invalid :
         {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        CHECK(DecodeYolo26RawHead(row.data(), 1, 5, invalid, 0.7f, 10).empty());
        CHECK(DecodeYolo26RawHead(row.data(), 1, 5, 0.25f, invalid, 10).empty());
    }
    CHECK(DecodeYolo26RawHead(row.data(), 1, 5, 0.25f, 1.1f, 10).empty());
}

}  // namespace
