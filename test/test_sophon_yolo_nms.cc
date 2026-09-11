#include "catch_amalgamated.hpp"
#include "nn/device/sophon/sophon_yolo_nms.h"

using cosmo::nn::SophonYoloNms;
using cosmo::nn::YoloBox;
using cosmo::nn::YoloBoxVec;

TEST_CASE("Sophon YOLO NMS suppresses unequal boxes using center coordinates", "[nn][sophon][nms]") {
    // The smaller box is contained: IoU = (70 * 90) / (80 * 200) = 0.39375.
    // Treating these centers as upper-left corners instead gives about 0.1234.
    YoloBox full{100.0f, 150.0f, 80.0f, 200.0f, 0.91f, 3};
    YoloBox part{100.0f, 95.0f, 70.0f, 90.0f, 0.82f, 3};

    SECTION("higher confidence full box survives") {}

    SECTION("higher confidence partial box survives") {
        std::swap(full.confidence, part.confidence);
    }

    SECTION("translation does not change suppression") {
        full.x += 200.25f;
        full.y -= 180.5f;
        part.x += 200.25f;
        part.y -= 180.5f;
    }

    const auto expected = full.confidence > part.confidence ? full : part;
    YoloBoxVec detections{part, full};
    const auto result = SophonYoloNms(detections, 0.35f);

    REQUIRE(result.size() == 1);
    // NMS must leave the surviving center, size and metadata unchanged for the parser.
    CHECK(result[0].x == expected.x);
    CHECK(result[0].y == expected.y);
    CHECK(result[0].width == expected.width);
    CHECK(result[0].height == expected.height);
    CHECK(result[0].confidence == expected.confidence);
    CHECK(result[0].class_id == expected.class_id);
}

TEST_CASE("Sophon YOLO NMS preserves adjacent boxes of different sizes", "[nn][sophon][nms]") {
    // Actual overlap is 40 * 140, giving IoU = 5600 / 22800, about 0.2456.
    // Treating the centers as upper-left corners inflates IoU to 0.42.
    YoloBoxVec detections{{140.0f, 100.0f, 60.0f, 140.0f, 0.87f, 0},
                          {100.0f, 100.0f, 100.0f, 200.0f, 0.91f, 0}};

    const auto result = SophonYoloNms(detections, 0.35f);

    REQUIRE(result.size() == 2);
    CHECK(result[0].confidence == 0.91f);
    CHECK(result[0].x == 100.0f);
    CHECK(result[1].confidence == 0.87f);
    CHECK(result[1].x == 140.0f);
}

TEST_CASE("Sophon YOLO NMS keeps the strict IoU threshold boundary", "[nn][sophon][nms]") {
    YoloBoxVec detections{{0.0f, 0.0f, 8.0f, 8.0f, 0.9f, 0}, {0.0f, 0.0f, 4.0f, 8.0f, 0.8f, 0}};

    SECTION("IoU equal to the threshold is retained") {
        CHECK(SophonYoloNms(detections, 0.5f).size() == 2);
    }

    SECTION("IoU above the threshold is suppressed") {
        CHECK(SophonYoloNms(detections, 0.49f).size() == 1);
    }
}

TEST_CASE("Sophon YOLO NMS retains touching and separate boxes", "[nn][sophon][nms]") {
    YoloBoxVec detections{{0.0f, 12.0f, 8.0f, 8.0f, 0.7f, 0},
                          {0.0f, 0.0f, 8.0f, 8.0f, 0.9f, 0},
                          {8.0f, 0.0f, 8.0f, 8.0f, 0.8f, 0}};

    const auto result = SophonYoloNms(detections, 0.0f);

    REQUIRE(result.size() == 3);
    CHECK(result[0].confidence == 0.9f);
    CHECK(result[1].confidence == 0.8f);
    CHECK(result[2].confidence == 0.7f);
}

TEST_CASE("Sophon YOLO NMS handles empty and single candidate input", "[nn][sophon][nms]") {
    YoloBoxVec detections;

    SECTION("empty input") {
        CHECK(SophonYoloNms(detections, 0.35f).empty());
    }

    SECTION("single candidate") {
        detections.push_back({1.5f, -2.25f, 4.0f, 6.0f, 0.9f, 2});
        const auto result = SophonYoloNms(detections, 0.35f);
        REQUIRE(result.size() == 1);
        CHECK(result[0].x == 1.5f);
        CHECK(result[0].y == -2.25f);
    }
}
