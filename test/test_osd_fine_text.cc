#include <algorithm>
#include <filesystem>
#include <limits>
#include <numeric>

#include "catch_amalgamated.hpp"
#include "media/OsdTextRenderer.h"

TEST_CASE("Fine OSD glyphs preserve original antialiasing without bold expansion",
          "[media][osd][fine-text]") {
    cosmo::media::OsdTextRenderer renderer;
    CHECK(renderer.RenderStringWithFineOutline("face", 16).bodyAlpha.empty());
    const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "3rd/fmt-7.1.2/doc/_static/fonts/glyphicons-halflings-regular.ttf";
    REQUIRE(renderer.Init(font.string()));
    // Use a real glyph from the bundled fixture font, without a host font dependency.
    const std::string text = "\xee\x80\x81";
    const auto original    = renderer.RenderString(text, 16);
    const auto legacy      = renderer.RenderStringWithOutline(text, 16);
    const auto fine        = renderer.RenderStringWithFineOutline(text, 16);
    REQUIRE_FALSE(original.alpha.empty());
    REQUIRE(fine.width == original.width + 2);
    REQUIRE(fine.height == original.height + 2);
    CHECK(fine.offsetX == 1);
    CHECK(fine.offsetY == 1);
    for (int y = 0; y < original.height; ++y)
        for (int x = 0; x < original.width; ++x)
            CHECK(fine.bodyAlpha[(y + 1) * fine.width + x + 1] == original.alpha[y * original.width + x]);
    CHECK(std::accumulate(fine.bodyAlpha.begin(), fine.bodyAlpha.end(), 0) ==
          std::accumulate(original.alpha.begin(), original.alpha.end(), 0));
    CHECK(std::accumulate(fine.bodyAlpha.begin(), fine.bodyAlpha.end(), 0) <
          std::accumulate(legacy.bodyAlpha.begin(), legacy.bodyAlpha.end(), 0));
    CHECK(*std::max_element(fine.outlineAlpha.begin(), fine.outlineAlpha.end()) <= 204);
    // Opaque boundary coverage is stronger than the former 65% edge, while
    // fully covered glyph pixels keep their original body and no dark overlay.
    CHECK(*std::max_element(fine.outlineAlpha.begin(), fine.outlineAlpha.end()) > 166);
    for (size_t i = 0; i < fine.bodyAlpha.size(); ++i)
        if (fine.bodyAlpha[i] == 255)
            CHECK(fine.outlineAlpha[i] == 0);
    CHECK(renderer.RenderStringWithOutline(text, 16).bodyAlpha == legacy.bodyAlpha);
    CHECK(renderer.RenderStringWithFineOutline(text, 16).bodyAlpha == fine.bodyAlpha);
    CHECK(renderer.RenderStringWithFineOutline(text, 16).outlineAlpha == fine.outlineAlpha);
    CHECK(
        renderer.RenderStringWithFineOutline(text, std::numeric_limits<float>::infinity()).bodyAlpha.empty());
}
