#include <limits>

#include "catch_amalgamated.hpp"
#include "flow/alarm/AlarmReviewRoi.h"
#include "media/PixelFormatUtils.h"
#include "util/DetectionGeometry.h"

TEST_CASE("Alarm review ROI keeps padding clipping and explicit axis aligned OBB geometry",
          "[alarm][laya-shadow][roi]") {
    using cosmo::AlarmReviewCropBox;
    auto middle = AlarmReviewCropBox(640, 480, {100, 80, 60, 100});
    CHECK(middle.x == 52);
    CHECK(middle.y == 32);
    CHECK(middle.width == 156);
    CHECK(middle.height == 196);
    auto edge = AlarmReviewCropBox(640, 480, {3, 5, 60, 100});
    CHECK(edge.x == 0);
    CHECK(edge.y == 0);
    CHECK(edge.width == 111);
    CHECK(edge.height == 153);
    auto outside = AlarmReviewCropBox(640, 480, {900, 20, 30, 40});
    CHECK(outside.empty());
    auto overflow = AlarmReviewCropBox(
        640, 480, {std::numeric_limits<int>::max(), 20, std::numeric_limits<int>::max(), 40});
    CHECK(overflow.empty());
    auto full = AlarmReviewCropBox(640, 480, {0, 0, 640, 480});
    CHECK(full == cosmo::util::Box(0, 0, 640, 480));
    auto corners = cosmo::util::MakeOrientedQuad(200, 100, 80, 40, 0.78539816339F);
    auto bounds  = cosmo::util::QuadBounds(corners);
    CHECK(bounds.x1 < 160);
    CHECK(bounds.x2 > 240);
    // Native crop aligns I420 coordinates; evidence must record this effective ROI, not the request.
    auto actual = cosmo::media::PixelFormatUtils::NormalizeCropRoi(
        640, 480, cosmo::media::PixelFormat::PIXEL_I420, edge, 16, 8192);
    REQUIRE(actual);
    CHECK(actual->width == 112);
    CHECK(actual->height == 154);
    CHECK(actual->x == 0);
    CHECK(actual->y == 0);
}
