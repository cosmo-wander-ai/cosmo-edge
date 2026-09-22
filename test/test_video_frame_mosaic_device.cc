#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#include "bmlib_runtime.h"
#include "media/IOsdTextRenderer.h"
#include "media/VideoFrameProcSophon.h"
#include "mem/AllocatorSophon.h"
#include "mem/DeviceContext.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "support/ScopedServiceOverride.h"

namespace cosmo::media {
namespace {

    class MosaicDeviceTextRenderer final : public IOsdTextRenderer {
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

    class MosaicDeviceFixture {
    public:
        MosaicDeviceFixture()
            : registration_(device_),
              pool_(std::make_unique<mem::AllocatorSophon>(device_), {64 * 64 * 3}),
              processor_(device_.GetMediaHandle(), renderer_) {
            mem::SetMemoryPoolContext(&pool_);
        }
        ~MosaicDeviceFixture() {
            mem::SetMemoryPoolContext(nullptr);
        }

        VideoFrameProcSophon& Processor() {
            return processor_;
        }

        void Upload(const VideoFramePtr& frame, const std::vector<uint8_t>& pixels) {
            REQUIRE(frame);
            REQUIRE(frame->GetSize() == pixels.size());
            auto* device_memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
            REQUIRE(device_memory);
            REQUIRE(bm_memcpy_s2d_partial(reinterpret_cast<bm_handle_t>(device_.GetMediaHandle()),
                                          *device_memory, const_cast<uint8_t*>(pixels.data()),
                                          static_cast<unsigned int>(pixels.size())) == BM_SUCCESS);
        }

        std::vector<uint8_t> Download(const VideoFramePtr& frame) {
            REQUIRE(frame);
            std::vector<uint8_t> pixels(frame->GetSize());
            auto* device_memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
            REQUIRE(device_memory);
            REQUIRE(bm_memcpy_d2s_partial(reinterpret_cast<bm_handle_t>(device_.GetMediaHandle()),
                                          pixels.data(), *device_memory,
                                          static_cast<unsigned int>(pixels.size())) == BM_SUCCESS);
            return pixels;
        }

    private:
        MosaicDeviceTextRenderer renderer_;
        mem::DeviceContext device_;
        test::ScopedServiceOverride<mem::IDeviceContext> registration_;
        mem::MemoryPoolMng pool_;
        VideoFrameProcSophon processor_;
    };

    std::vector<uint8_t> DevicePattern(PixelFormat format) {
        constexpr size_t luma_size = 64 * 64;
        const bool rgb             = format == PixelFormat::PIXEL_RGB8;
        std::vector<uint8_t> pixels(rgb ? luma_size * 3 : luma_size * 3 / 2);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const auto offset = static_cast<size_t>(y) * 64 + x;
                if (rgb) {
                    pixels[offset * 3]     = static_cast<uint8_t>(x);
                    pixels[offset * 3 + 1] = static_cast<uint8_t>(y);
                    pixels[offset * 3 + 2] = static_cast<uint8_t>(x + y);
                } else {
                    pixels[offset] = static_cast<uint8_t>(x + y);
                }
            }
        }
        if (!rgb) {
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 32; ++x) {
                    const auto offset                          = static_cast<size_t>(y) * 32 + x;
                    pixels[luma_size + offset]                 = static_cast<uint8_t>(40 + x + 2 * y);
                    pixels[luma_size + luma_size / 4 + offset] = static_cast<uint8_t>(100 + x + 2 * y);
                }
            }
        }
        return pixels;
    }

    void CheckDeviceMosaic(const std::vector<uint8_t>& original, const std::vector<uint8_t>& masked,
                           PixelFormat format) {
        REQUIRE(masked.size() == original.size());
        const bool rgb = format == PixelFormat::PIXEL_RGB8;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const bool first  = x >= 10 && x < 22 && y >= 10 && y < 22;
                const bool second = x >= 38 && x < 50 && y >= 38 && y < 50;
                const auto offset = static_cast<size_t>(y) * 64 + x;
                if (rgb) {
                    CHECK(masked[offset * 3] == (first ? 15 : second ? 43 : original[offset * 3]));
                    CHECK(masked[offset * 3 + 1] == (first ? 15 : second ? 43 : original[offset * 3 + 1]));
                    CHECK(masked[offset * 3 + 2] == (first ? 31 : second ? 87 : original[offset * 3 + 2]));
                } else {
                    CHECK(masked[offset] == (first ? 31 : second ? 87 : original[offset]));
                }
            }
        }
        if (!rgb) {
            constexpr size_t luma_size = 64 * 64;
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 32; ++x) {
                    const bool first  = x >= 5 && x < 11 && y >= 5 && y < 11;
                    const bool second = x >= 19 && x < 25 && y >= 19 && y < 25;
                    const auto u      = luma_size + static_cast<size_t>(y) * 32 + x;
                    const auto v      = u + luma_size / 4;
                    CHECK(masked[u] == (first ? 62 : second ? 104 : original[u]));
                    CHECK(masked[v] == (first ? 122 : second ? 164 : original[v]));
                }
            }
        }
    }

}  // namespace

TEST_CASE("Sophon mosaics a private device copy without changing shared RGB or YUV frames",
          "[.device][mosaic-device][privacy]") {
    MosaicDeviceFixture fixture;
    for (const auto format : {PixelFormat::PIXEL_RGB8, PixelFormat::PIXEL_I420}) {
        INFO("pixel format=" << static_cast<int>(format));
        auto source = std::make_shared<VideoFrame>(64, 64, format, 123456, 987654321);
        REQUIRE(VideoFrameValid(source));
        source->SetStreamIndex(8);
        const auto original = DevicePattern(format);
        fixture.Upload(source, original);
        REQUIRE(source->GetHostData() == nullptr);

        auto masked = fixture.Processor().MosaicCopy(source, {{12, 12, 8, 8}, {40, 40, 8, 8}}, 2);
        REQUIRE(VideoFrameValid(masked));
        CHECK(masked != source);
        CHECK(masked->GetData() != source->GetData());
        CHECK(masked->GetPixelFormat() == source->GetPixelFormat());
        CHECK(masked->GetWidth() == source->GetWidth());
        CHECK(masked->GetHeight() == source->GetHeight());
        CHECK(masked->GetFrameIndex() == source->GetFrameIndex());
        CHECK(masked->GetTimestamp() == source->GetTimestamp());
        CHECK(masked->GetStreamIndex() == source->GetStreamIndex());
        CHECK(source->GetHostData() == nullptr);
        CheckDeviceMosaic(original, fixture.Download(masked), format);
        CHECK(fixture.Download(source) == original);

        // An unrelated stale host cache must neither become the output source
        // nor be overwritten by mosaic's device-to-host transfer.
        auto* stale_host = static_cast<uint8_t*>(std::malloc(source->GetSize()));
        REQUIRE(stale_host != nullptr);
        std::fill_n(stale_host, source->GetSize(), uint8_t{7});
        source->SetHostData(stale_host);
        auto zero_targets = fixture.Processor().MosaicCopy(source, {}, 2);
        REQUIRE(VideoFrameValid(zero_targets));
        CHECK(zero_targets != source);
        CHECK(zero_targets->GetData() != source->GetData());
        CHECK(fixture.Download(zero_targets) == original);
        CHECK(source->GetHostData() == stale_host);
        CHECK(std::all_of(stale_host, stale_host + source->GetSize(),
                          [](uint8_t value) { return value == 7; }));

        CHECK(fixture.Processor().MosaicCopy(source, {{12, 12, 8, 8}, {64, 0, 8, 8}}, 2) == nullptr);
        CHECK(fixture.Processor().MosaicCopy(source, {{0, 0, 0, 8}}, 2) == nullptr);
        CHECK(fixture.Processor().MosaicCopy(source, {}, 4) == nullptr);
        CHECK(fixture.Download(source) == original);
        CHECK(std::all_of(stale_host, stale_host + source->GetSize(),
                          [](uint8_t value) { return value == 7; }));
    }
    CHECK(fixture.Processor().MosaicCopy(nullptr, {}, 2) == nullptr);
}

}  // namespace cosmo::media
#endif
