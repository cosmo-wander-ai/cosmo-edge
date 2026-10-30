#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/OsdLineRasterizer.h"

namespace {
using cosmo::media::OsdLineGroup;
using cosmo::media::RasterizeOsdLinesI420;

struct TestI420 {
    int width, height;
    std::vector<uint8_t> y, u, v;
    TestI420(int w, int h)
        : width(w),
          height(h),
          y(static_cast<size_t>(w) * h, 128),
          u(static_cast<size_t>(w / 2) * (h / 2), 128),
          v(u) {}
    void Draw(const std::vector<OsdLineGroup>& groups) {
        RasterizeOsdLinesI420(y.data(), u.data(), v.data(), width, height, groups);
    }
    uint8_t Y(int x, int row) const {
        return y[static_cast<size_t>(row) * width + x];
    }
};

TEST_CASE("CPU I420 OSD antialiases geometry and preserves color in chroma blocks",
          "[media][osd][cpu-lines]") {
    TestI420 image(64, 48);
    image.Draw({{{{{5, 8}, {45, 30}}}, {255, 107, 107}, 2}});
    CHECK(image.Y(5, 8) != 128);
    CHECK(image.Y(45, 30) != 128);
    CHECK(image.Y(60, 2) == 128);
    CHECK(std::any_of(image.u.begin(), image.u.end(), [](uint8_t p) { return p != 128; }));
    CHECK(std::any_of(image.v.begin(), image.v.end(), [](uint8_t p) { return p != 128; }));
    // Smooth coverage makes the diagonal contain intermediate luminance values.
    CHECK(std::any_of(image.y.begin(), image.y.end(), [](uint8_t p) { return p != 128 && p != 151; }));
}

TEST_CASE("CPU I420 OSD covers original geometry across segment directions", "[media][osd][cpu-lines]") {
    using Line                    = std::pair<cosmo::util::Point, cosmo::util::Point>;
    const std::vector<Line> lines = {
        {{2, 2}, {59, 2}},       {{59, 5}, {3, 42}},      {{10, 4}, {10, 43}},  {{40, 42}, {1, 3}},
        {{-100, 10}, {100, 30}}, {{30, -100}, {30, 100}}, {{32, 24}, {32, 24}},
    };
    for (const auto& segment : lines) {
        INFO("line " << segment.first.x << "," << segment.first.y << " -> " << segment.second.x << ","
                     << segment.second.y);
        TestI420 image(64, 48);
        image.Draw({{{segment}, {255, 107, 107}, 2}});
        const double dx      = static_cast<double>(segment.second.x) - segment.first.x;
        const double dy      = static_cast<double>(segment.second.y) - segment.first.y;
        const double length2 = dx * dx + dy * dy;
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const double projection =
                    length2 == 0
                        ? 0.0
                        : std::clamp(((x - segment.first.x) * dx + (y - segment.first.y) * dy) / length2, 0.0,
                                     1.0);
                const double ex       = x - (segment.first.x + projection * dx);
                const double ey       = y - (segment.first.y + projection * dy);
                const double distance = std::sqrt(ex * ex + ey * ey);
                if (distance <= 0.49)
                    CHECK(image.Y(x, y) == 151);
                if (distance >= 3.01)
                    CHECK(image.Y(x, y) == 128);
            }
        }
    }
}

TEST_CASE("CPU I420 OSD clips extreme segments without inventing border geometry",
          "[media][osd][cpu-lines]") {
    TestI420 image(64, 48);
    const auto low  = std::numeric_limits<int>::min();
    const auto high = std::numeric_limits<int>::max();
    image.Draw({{{{{low, 20}, {high, 20}}, {{-1000, -1000}, {-500, -500}}}, {168, 164, 255}, 2}});
    CHECK(image.Y(0, 20) != 128);
    CHECK(image.Y(63, 20) != 128);
    CHECK(image.Y(0, 2) == 128);
    CHECK(image.Y(63, 45) == 128);
    CHECK(image.Y(32, 2) == 128);
}

TEST_CASE("CPU I420 OSD retains all lines beyond SDK batch limits and color order",
          "[media][osd][cpu-lines]") {
    TestI420 image(512, 512);
    OsdLineGroup many;
    many.color = {34, 211, 238};
    many.width = 2;
    for (int i = 0; i < 150; ++i)
        many.lines.push_back({{10, i * 3 + 10}, {30, i * 3 + 10}});
    image.Draw({many, {{{{10, 457}, {30, 457}}}, {255, 107, 107}, 2}});
    CHECK(image.Y(20, 10) != 128);
    CHECK(image.Y(20, 10 + 149 * 3) != 128);  // Line 150, not discarded after 128.
    CHECK(image.Y(20, 457) == 151);           // The later coral group paints over cyan.
}

TEST_CASE("CPU I420 OSD tolerates empty, invalid, and partial chroma edge inputs",
          "[media][osd][cpu-lines]") {
    TestI420 image(5, 5);
    const auto original = image.y;
    image.Draw({{{{{0, 0}, {4, 4}}}, {255, 107, 107}, 0}});
    CHECK(image.y == original);
    RasterizeOsdLinesI420(nullptr, image.u.data(), image.v.data(), 5, 5, {});
    image.Draw({{{{{0, 0}, {4, 4}}}, {255, 107, 107}, 2}});
    CHECK(image.Y(4, 4) != 128);
    CHECK(image.u.size() == 4);
    CHECK(image.v.size() == 4);
}

TEST_CASE("CPU OSD rectangle blends once, clips boundaries and preserves unrelated pixels",
          "[media][osd][filled-rect]") {
    TestI420 image(8, 8);
    cosmo::media::RasterizeOsdRectI420(image.y.data(), image.u.data(), image.v.data(), 8, 8, {-2, 1, 5, 4},
                                       {0, 0, 0}, 128);
    CHECK(image.Y(0, 1) == 64);
    CHECK(image.Y(2, 4) == 64);
    CHECK(image.Y(3, 4) == 128);
    CHECK(image.Y(0, 0) == 128);
    CHECK(image.Y(7, 7) == 128);
    const auto unchanged = image.y;
    cosmo::media::RasterizeOsdRectI420(image.y.data(), image.u.data(), image.v.data(), 8, 8,
                                       {std::numeric_limits<int>::max(), 2, 10, 10}, {0, 0, 0}, 255);
    CHECK(image.y == unchanged);
    TestI420 onePixel(8, 8);
    cosmo::media::RasterizeOsdRectI420(onePixel.y.data(), onePixel.u.data(), onePixel.v.data(), 8, 8,
                                       {0, 0, 1, 1}, {0, 0, 255}, 255);
    CHECK(onePixel.u[0] == 160);  // 25% coverage of this 2x2 chroma sample.
    CHECK(onePixel.u[1] == 128);
}

TEST_CASE("Rounded outline is a translucent one-pixel ring, with transparent interior",
          "[media][osd][rounded-outline]") {
    using cosmo::media::RasterizeOsdRoundedRectOutlineI420;
    using cosmo::media::RasterizeOsdRoundedRectOutlineRgb;
    TestI420 image(12, 12);
    RasterizeOsdRoundedRectOutlineI420(image.y.data(), image.u.data(), image.v.data(), 12, 12, {2, 2, 8, 8},
                                       {0, 0, 255}, 153, 3);
    CHECK(image.Y(2, 2) == 128);  // Outside the rounded corner.
    CHECK(image.Y(5, 5) == 128);  // Interior never gets painted or repainted.
    CHECK(image.Y(5, 2) == 69);   // Full ring coverage, one alpha blend.
    CHECK(image.Y(3, 2) > 69);    // Smooth fractional corner coverage.
    CHECK(image.Y(3, 2) < 128);
    CHECK(image.u[2 * 6 + 2] == 128);  // A fully interior chroma block is untouched.
    TestI420 onePixel(4, 4);
    RasterizeOsdRoundedRectOutlineI420(onePixel.y.data(), onePixel.u.data(), onePixel.v.data(), 4, 4,
                                       {0, 0, 1, 1}, {0, 0, 255}, 255, 0);
    CHECK(onePixel.u[0] == 160);  // One of four luma pixels: mean UV coverage, not full blue.
    CHECK(onePixel.u[1] == 128);

    for (const bool bgr : {false, true}) {
        std::vector<uint8_t> rgb(12 * 12 * 3, 128);
        RasterizeOsdRoundedRectOutlineRgb(rgb.data(), 12, 12, bgr, {2, 2, 8, 8}, {200, 100, 50}, 128, 3);
        const auto channel = [&](int x, int y, int n) { return rgb[(y * 12 + x) * 3 + n]; };
        CHECK(channel(5, 5, 0) == 128);
        CHECK(channel(2, 2, 0) == 128);
        CHECK(channel(5, 2, bgr ? 2 : 0) == 164);
        CHECK(channel(5, 2, 1) == 114);
        CHECK(channel(5, 2, bgr ? 0 : 2) == 89);
        // Drawing an outline over an existing translucent panel preserves its entire center.
        const auto center = channel(5, 5, 0);
        RasterizeOsdRoundedRectOutlineRgb(rgb.data(), 12, 12, bgr, {2, 2, 8, 8}, {255, 255, 255}, 255, 3);
        CHECK(channel(5, 5, 0) == center);
    }
}

TEST_CASE("Rounded outline clipping does not create an image-edge border or corrupt odd chroma",
          "[media][osd][rounded-outline]") {
    using cosmo::media::RasterizeOsdRoundedRectOutlineI420;
    TestI420 image(5, 5);
    RasterizeOsdRoundedRectOutlineI420(image.y.data(), image.u.data(), image.v.data(), 5, 5, {-2, -2, 10, 10},
                                       {0, 0, 255}, 255, 2);
    CHECK(std::all_of(image.y.begin(), image.y.end(), [](uint8_t pixel) { return pixel == 128; }));
    CHECK(std::all_of(image.u.begin(), image.u.end(), [](uint8_t pixel) { return pixel == 128; }));
    RasterizeOsdRoundedRectOutlineI420(image.y.data(), image.u.data(), image.v.data(), 5, 5, {1, 1, 4, 4},
                                       {0, 0, 255}, 255, 1);
    CHECK(image.Y(4, 2) != 128);  // Odd terminal luma pixel has no corresponding complete UV block.
    const auto pixels = image.y;
    for (const auto& rect : std::vector<cosmo::util::Box>{{std::numeric_limits<int>::max(), 0, 4, 4},
                                                          {std::numeric_limits<int>::min(), 0, 4, 4},
                                                          {0, 0, -1, 4}}) {
        RasterizeOsdRoundedRectOutlineI420(image.y.data(), image.u.data(), image.v.data(), 5, 5, rect,
                                           {0, 0, 0}, 255, 3);
    }
    RasterizeOsdRoundedRectOutlineI420(image.y.data(), image.u.data(), image.v.data(), 5, 5, {0, 0, 5, 5},
                                       {0, 0, 0}, 0, 3);
    CHECK(image.y == pixels);
    RasterizeOsdRoundedRectOutlineI420(nullptr, image.u.data(), image.v.data(), 5, 5, {0, 0, 5, 5}, {0, 0, 0},
                                       255, 3);
}

TEST_CASE("Rounded OSD panels preserve corner transparency and blend chroma once",
          "[media][osd][rounded-rect]") {
    TestI420 image(12, 12);
    cosmo::media::RasterizeOsdRoundedRectI420(image.y.data(), image.u.data(), image.v.data(), 12, 12,
                                              {2, 2, 8, 8}, {0, 0, 255}, 153, 3);
    CHECK(image.Y(2, 2) == 128);           // Rounded corner stays fully transparent.
    CHECK(image.Y(5, 5) == 69);            // 60% blue luma, blended exactly once.
    CHECK(image.Y(3, 2) > image.Y(5, 5));  // Antialiased curved edge.
    CHECK(image.Y(3, 2) < 128);
    CHECK(image.u[2 * 6 + 2] == 204);
    CHECK(image.Y(1, 2) == 128);
    TestI420 squared(12, 12), zeroRadius(12, 12);
    cosmo::media::RasterizeOsdRectI420(squared.y.data(), squared.u.data(), squared.v.data(), 12, 12,
                                       {-2, 1, 8, 8}, {52, 50, 71}, 153);
    cosmo::media::RasterizeOsdRoundedRectI420(zeroRadius.y.data(), zeroRadius.u.data(), zeroRadius.v.data(),
                                              12, 12, {-2, 1, 8, 8}, {52, 50, 71}, 153, 0);
    CHECK(squared.y == zeroRadius.y);
    CHECK(squared.u == zeroRadius.u);
    CHECK(squared.v == zeroRadius.v);

    TestI420 odd(5, 5);
    cosmo::media::RasterizeOsdRoundedRectI420(odd.y.data(), odd.u.data(), odd.v.data(), 5, 5,
                                              {-2, -2, 10, 10}, {0, 0, 0}, 128, 3);
    CHECK(odd.Y(4, 4) == 64);
    const auto before = odd.y;
    cosmo::media::RasterizeOsdRoundedRectI420(odd.y.data(), odd.u.data(), odd.v.data(), 5, 5,
                                              {std::numeric_limits<int>::max(), 0, 12, 12}, {0, 0, 0}, 255,
                                              3);
    CHECK(odd.y == before);
}

TEST_CASE("Rounded OSD packed RGB and BGR use matching fractional coverage", "[media][osd][rounded-rect]") {
    std::vector<uint8_t> rgb(12 * 12 * 3, 128), bgr(rgb);
    cosmo::media::RasterizeOsdRoundedRectRgb(rgb.data(), 12, 12, false, {2, 2, 8, 8}, {52, 50, 71}, 153, 3);
    cosmo::media::RasterizeOsdRoundedRectRgb(bgr.data(), 12, 12, true, {2, 2, 8, 8}, {52, 50, 71}, 153, 3);
    for (size_t i = 0; i < rgb.size(); i += 3) {
        CHECK(rgb[i] == bgr[i + 2]);
        CHECK(rgb[i + 1] == bgr[i + 1]);
        CHECK(rgb[i + 2] == bgr[i]);
    }
    CHECK(rgb[(2 * 12 + 2) * 3] == 128);
    CHECK(rgb[(5 * 12 + 5) * 3] == 82);
}

TEST_CASE("Fine OSD bitmap composition averages all four chroma pixels and clips offsets",
          "[media][osd][fine-text]") {
    cosmo::media::IOsdTextRenderer::OutlinedTextBitmap bitmap;
    bitmap.width = bitmap.height = 2;
    bitmap.offsetX = bitmap.offsetY = 0;
    bitmap.bodyAlpha                = {255, 0, 0, 0};
    bitmap.outlineAlpha             = {0, 0, 0, 0};
    TestI420 image(8, 8);
    cosmo::media::RasterizeOsdTextI420(image.y.data(), image.u.data(), image.v.data(), 8, 8, 2, 2, bitmap,
                                       {0, 0, 255});
    CHECK(image.Y(2, 2) == 29);
    CHECK(image.Y(3, 2) == 128);
    CHECK(image.u[1 * 4 + 1] == 160);  // One blue pixel covers 25% of a chroma sample.
    CHECK(image.u[0] == 128);
    const auto before = image.y;
    cosmo::media::RasterizeOsdTextI420(image.y.data(), image.u.data(), image.v.data(), 8, 8,
                                       std::numeric_limits<int>::min(), 2, bitmap, {255, 255, 255});
    CHECK(image.y == before);
    bitmap.bodyAlpha.pop_back();
    cosmo::media::RasterizeOsdTextI420(image.y.data(), image.u.data(), image.v.data(), 8, 8, 2, 2, bitmap,
                                       {255, 255, 255});
    CHECK(image.y == before);  // Malformed buffers are ignored.
}
TEST_CASE("Fine OSD chroma preserves adjacent outline and body coverage", "[media][osd][fine-text]") {
    TestI420 image(8, 8);
    std::fill(image.u.begin(), image.u.end(), 0);
    cosmo::media::IOsdTextRenderer::OutlinedTextBitmap bitmap;
    bitmap.width = bitmap.height = 2;
    bitmap.offsetX = bitmap.offsetY = 0;
    bitmap.bodyAlpha                = {255, 255, 0, 0};
    bitmap.outlineAlpha             = {0, 0, 166, 166};
    cosmo::media::RasterizeOsdTextI420(image.y.data(), image.u.data(), image.v.data(), 8, 8, 2, 2, bitmap,
                                       {0, 0, 255});
    // Two fully blue pixels plus two independently outlined pixels in the same
    // chroma block. Text coverage must not darken the neighbours' outlines.
    CHECK(image.u[1 * 4 + 1] == 169);
    CHECK(image.u[0] == 0);
}
TEST_CASE("Fine target strokes keep antialiasing without a dark halo", "[media][osd][fine-lines]") {
    TestI420 fine(32, 32), legacy(32, 32);
    OsdLineGroup group{{{{5, 10}, {25, 10}}}, {85, 204, 216}, 2, true};
    fine.Draw({group});
    group.fine = false;
    legacy.Draw({group});
    CHECK(fine.Y(15, 8) == 128);
    CHECK(legacy.Y(15, 8) != 128);
    CHECK(fine.Y(15, 10) == legacy.Y(15, 10));
    CHECK(fine.Y(15, 9) > 128);
    CHECK(fine.Y(15, 9) < fine.Y(15, 10));
    std::vector<uint8_t> rgb(32 * 32 * 3, 128), bgr(rgb);
    group.fine = true;
    cosmo::media::RasterizeOsdLinesRgb(rgb.data(), 32, 32, false, {group});
    cosmo::media::RasterizeOsdLinesRgb(bgr.data(), 32, 32, true, {group});
    CHECK(rgb[(8 * 32 + 15) * 3] == 128);
    CHECK(rgb[(10 * 32 + 15) * 3] == 85);
    CHECK(rgb[(9 * 32 + 15) * 3] > 85);
    CHECK(rgb[(9 * 32 + 15) * 3] < 128);
    CHECK(bgr[(10 * 32 + 15) * 3 + 2] == 85);
}
}  // namespace
