#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/MosaicPixels.h"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND) || defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
#include "media/IOsdTextRenderer.h"
#include "media/VideoFrameProcCpu.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#endif

namespace cosmo::media {
namespace {

    std::vector<uint8_t> RgbGradient(int width, int height) {
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto offset  = (static_cast<size_t>(y) * width + x) * 3;
                pixels[offset]     = static_cast<uint8_t>(x);
                pixels[offset + 1] = static_cast<uint8_t>(y);
                pixels[offset + 2] = static_cast<uint8_t>(x + y);
            }
        }
        return pixels;
    }

}  // namespace

TEST_CASE("Mosaic covers multiple targets and protective margins without changing the background",
          "[mosaic][privacy]") {
    for (const auto format : {PixelFormat::PIXEL_RGB8, PixelFormat::PIXEL_BGR8}) {
        auto pixels         = RgbGradient(64, 64);
        const auto original = pixels;
        REQUIRE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 64, 64, format,
                                    {{12, 12, 8, 8}, {40, 40, 8, 8}}, 2));
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const auto offset = (static_cast<size_t>(y) * 64 + x) * 3;
                if (x >= 10 && x < 22 && y >= 10 && y < 22) {
                    CHECK(pixels[offset] == 15);
                    CHECK(pixels[offset + 1] == 15);
                    CHECK(pixels[offset + 2] == 31);
                } else if (x >= 38 && x < 50 && y >= 38 && y < 50) {
                    CHECK(pixels[offset] == 43);
                    CHECK(pixels[offset + 1] == 43);
                    CHECK(pixels[offset + 2] == 87);
                } else {
                    CHECK(pixels[offset] == original[offset]);
                    CHECK(pixels[offset + 1] == original[offset + 1]);
                    CHECK(pixels[offset + 2] == original[offset + 2]);
                }
            }
        }
    }
}

TEST_CASE("Mosaic averages every YUV plane and expands odd boxes outward", "[mosaic][privacy]") {
    constexpr size_t luma_size = 32 * 32;
    for (const auto format : {PixelFormat::PIXEL_I420, PixelFormat::PIXEL_NV12, PixelFormat::PIXEL_NV21}) {
        std::vector<uint8_t> pixels(luma_size * 3 / 2);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                pixels[y * 32 + x] = static_cast<uint8_t>(x + 2 * y);
            }
        }
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto chroma = static_cast<size_t>(y) * 16 + x;
                if (format == PixelFormat::PIXEL_I420) {
                    pixels[luma_size + chroma]                 = static_cast<uint8_t>(x + 8 * y);
                    pixels[luma_size + luma_size / 4 + chroma] = static_cast<uint8_t>(100 + x + 8 * y);
                } else {
                    pixels[luma_size + chroma * 2]     = static_cast<uint8_t>(x + 8 * y);
                    pixels[luma_size + chroma * 2 + 1] = static_cast<uint8_t>(100 + x + 8 * y);
                }
            }
        }
        const auto original = pixels;
        REQUIRE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 32, 32, format, {{5, 5, 6, 6}}, 2));
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const bool covered = x >= 2 && x < 14 && y >= 2 && y < 14;
                CHECK(pixels[y * 32 + x] == (covered ? 22 : original[y * 32 + x]));
            }
        }
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const bool covered = x >= 1 && x < 7 && y >= 1 && y < 7;
                const auto chroma  = static_cast<size_t>(y) * 16 + x;
                const auto first   = luma_size + (format == PixelFormat::PIXEL_I420 ? chroma : chroma * 2);
                const auto second =
                    format == PixelFormat::PIXEL_I420 ? luma_size + luma_size / 4 + chroma : first + 1;
                CHECK(pixels[first] == (covered ? 31 : original[first]));
                CHECK(pixels[second] == (covered ? 131 : original[second]));
            }
        }
    }
}

TEST_CASE("Mosaic strength increases block size", "[mosaic][privacy]") {
    for (int strength = 1; strength <= 3; ++strength) {
        auto pixels = RgbGradient(64, 64);
        REQUIRE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 64, 64, PixelFormat::PIXEL_RGB8,
                                    {{0, 0, 64, 64}}, strength));
        CHECK(pixels[0] == 2 * strength + 1);
        CHECK(pixels[3 * 8] == (strength == 1 ? 11 : pixels[0]));
    }
}

TEST_CASE("Invalid mosaic input fails before writing any pixels", "[mosaic][privacy]") {
    auto pixels         = RgbGradient(32, 32);
    const auto original = pixels;
    const std::vector<util::Box> invalid_boxes{{1, 1, 0, 10},
                                               {1, 1, 10, -1},
                                               {32, 0, 4, 4},
                                               {-10, 0, 10, 4},
                                               {std::numeric_limits<int>::max(), 0, 20, 20}};
    for (const auto& box : invalid_boxes) {
        CHECK_FALSE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 32, 32, PixelFormat::PIXEL_RGB8,
                                        {{4, 4, 8, 8}, box}, 2));
        CHECK(pixels == original);
    }
    CHECK_FALSE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 32, 32, PixelFormat::PIXEL_RGB8, {}, 0));
    CHECK_FALSE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 32, 32, PixelFormat::PIXEL_RGB8, {}, 4));
    CHECK_FALSE(
        ApplyMosaicToPixels(pixels.data(), pixels.size() - 1, 32, 32, PixelFormat::PIXEL_RGB8, {}, 2));
    CHECK_FALSE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 32, 32, PixelFormat::PIXEL_RGB32F, {}, 2));
    CHECK_FALSE(ApplyMosaicToPixels(nullptr, pixels.size(), 32, 32, PixelFormat::PIXEL_RGB8, {}, 2));
    CHECK(pixels == original);
    REQUIRE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 32, 32, PixelFormat::PIXEL_RGB8, {}, 2));
    CHECK(pixels == original);
}

TEST_CASE("Mosaic clips border targets with wide integer arithmetic", "[mosaic][privacy]") {
    auto pixels = RgbGradient(32, 32);
    REQUIRE(ApplyMosaicToPixels(pixels.data(), pixels.size(), 32, 32, PixelFormat::PIXEL_RGB8,
                                {{-2, -2, 6, 6}, {30, 30, std::numeric_limits<int>::max(), 10}}, 2));
    CHECK(pixels[0] == pixels[3 * 4]);
    CHECK(pixels[(31 * 32 + 31) * 3] != 31);
}

#if defined(COSMO_MEDIA_USE_CPU_BACKEND) || defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
namespace {

    class MosaicTextRenderer final : public IOsdTextRenderer {
    public:
        bool Init(const std::string&) override {
            return true;
        }
        bool IsReady() const override {
            return false;
        }
        TextBitmap RenderString(const std::string&, float) const override {
            return {};
        }
        OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
            return {};
        }
    };

    class MosaicCpuFixture {
    public:
        MosaicCpuFixture() : pool_(std::make_unique<mem::AllocatorCpu>(), {64 * 64 * 3}), proc_(renderer_) {
            mem::SetMemoryPoolContext(&pool_);
        }
        ~MosaicCpuFixture() {
            mem::SetMemoryPoolContext(nullptr);
        }
        VideoFrameProcCpu& Processor() {
            return proc_;
        }

    private:
        mem::MemoryPoolMng pool_;
        MosaicTextRenderer renderer_;
        VideoFrameProcCpu proc_;
    };

}  // namespace

TEST_CASE("MosaicCopy keeps shared pixels untouched and preserves exact frame identity",
          "[mosaic][privacy][cpu]") {
    MosaicCpuFixture fixture;
    auto src = std::make_shared<VideoFrame>(64, 64, PixelFormat::PIXEL_RGB8, 123, 456);
    REQUIRE(src->Active());
    src->SetStreamIndex(789);
    const auto original = RgbGradient(64, 64);
    std::copy(original.begin(), original.end(), src->GetData());

    auto result = fixture.Processor().MosaicCopy(src, {{12, 12, 8, 8}}, 2);
    REQUIRE(result);
    CHECK(result != src);
    CHECK(result->GetData() != src->GetData());
    CHECK(result->GetFrameIndex() == 123);
    CHECK(result->GetTimestamp() == 456);
    CHECK(result->GetStreamIndex() == 789);
    CHECK(result->GetWidth() == src->GetWidth());
    CHECK(result->GetHeight() == src->GetHeight());
    CHECK(result->GetPixelFormat() == src->GetPixelFormat());
    CHECK(std::equal(original.begin(), original.end(), src->GetData()));
    CHECK_FALSE(std::equal(original.begin(), original.end(), result->GetData()));

    auto empty = fixture.Processor().MosaicCopy(src, {}, 2);
    REQUIRE(empty);
    CHECK(empty != src);
    CHECK(empty->GetData() != src->GetData());
    CHECK(std::equal(original.begin(), original.end(), empty->GetData()));
    CHECK(fixture.Processor().MosaicCopy(src, {{12, 12, 8, 8}, {64, 0, 8, 8}}, 2) == nullptr);
    CHECK(std::equal(original.begin(), original.end(), src->GetData()));
    CHECK(fixture.Processor().MosaicCopy(nullptr, {}, 2) == nullptr);
    CHECK(fixture.Processor().MosaicCopy(src, {}, 4) == nullptr);
    src->SetWidth(63);
    CHECK(fixture.Processor().MosaicCopy(src, {}, 2) == nullptr);
}
#endif

}  // namespace cosmo::media
