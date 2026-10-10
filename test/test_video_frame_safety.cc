#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "infer/AiComponment.h"
#include "media/EncodedImageInfo.h"
#include "media/PixelFormatUtils.h"
#include "media/VideoDecoder.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "util/CipherUtil.h"
#include "util/VideoInfo.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include <chrono>
#include <vector>

#include "bmlib_runtime.h"
#include "media/IOsdTextRenderer.h"
#include "media/VideoEncoder.h"
#include "media/VideoFrameProcSophon.h"
#include "mem/DeviceContext.h"
#include "mem/IDeviceContext.h"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/impl/MemoryPoolServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#endif

namespace cosmo::media {

namespace {

    class RejectingDecoder final : public VideoDecoder {
    public:
        RejectingDecoder() : VideoDecoder(0) {}

        bool Open() override {
            return true;
        }

        bool Close() override {
            return true;
        }

        bool IsOpened() override {
            return true;
        }

        bool SendPacket(const uint8_t*, size_t, int64_t) override {
            return false;
        }

        VideoFramePtr GetFrame() override {
            return nullptr;
        }
    };

}  // namespace

TEST_CASE("Decoder failure logging handles empty and short packets", "[video-frame-safety]") {
    RejectingDecoder decoder;
    bool result = true;
    REQUIRE(decoder.Decode(nullptr, 0, 1, result) == nullptr);
    REQUIRE_FALSE(result);

    const uint8_t byte = 0x7f;
    result             = true;
    REQUIRE(decoder.Decode(&byte, 1, 2, result) == nullptr);
    REQUIRE_FALSE(result);
}

TEST_CASE("Deferred decoded frames materialize or discard exactly once", "[video-frame-safety][decoder]") {
    int materialize_count = 0;
    int discard_count     = 0;
    DecodedVideoFrame discarded(
        41, 1920, 1080, PixelFormat::PIXEL_I420,
        [&]() {
            materialize_count++;
            return VideoFramePtr{};
        },
        [&]() { discard_count++; });

    REQUIRE(discarded.HasFrame());
    REQUIRE(discarded.IsDeferred());
    CHECK(discarded.GetFrameIndex() == 41);
    CHECK(discarded.GetWidth() == 1920);
    CHECK(discarded.GetHeight() == 1080);
    discarded.Discard();
    discarded.Discard();
    CHECK(materialize_count == 0);
    CHECK(discard_count == 1);
    CHECK_FALSE(discarded.HasFrame());

    DecodedVideoFrame materialized(
        42, 1280, 720, PixelFormat::PIXEL_I420,
        [&]() {
            materialize_count++;
            return VideoFramePtr{};
        },
        [&]() { discard_count++; });
    CHECK(materialized.Materialize() == nullptr);
    materialized.Discard();
    CHECK(materialize_count == 1);
    CHECK(discard_count == 1);
}

TEST_CASE("Deferred host pixels preserve identity and materialize once across consumers",
          "[video-frame-safety][deferred-host]") {
    mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {48});
    mem::SetMemoryPoolContext(&pool);
    struct ResetPool {
        ~ResetPool() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } reset_pool;
    auto source = std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456);
    REQUIRE(source->Active());
    source->SetStreamIndex(7);
    std::fill_n(source->GetData(), source->GetSize(), uint8_t{93});
    auto* expected_data                       = source->GetData();
    std::weak_ptr<VideoFrame> retained_source = source;
    std::atomic<int> calls{0};
    auto deferred =
        std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456, [source, &calls]() {
            ++calls;
            return source;
        });
    deferred->SetStreamIndex(7);
    source.reset();
    CHECK_FALSE(retained_source.expired());
    CHECK(deferred->Active());
    CHECK(deferred->IsDeferred());
    CHECK(deferred->GetFrameIndex() == 42);
    CHECK(deferred->GetTimestamp() == 123456);
    CHECK(deferred->GetStreamIndex() == 7);
    CHECK(deferred->GetWidth() == 4);
    CHECK(deferred->GetHeight() == 4);
    CHECK(deferred->GetSize() == 48);
    CHECK(calls == 0);

    std::array<uint8_t*, 8> pixels{};
    std::vector<std::thread> consumers;
    for (size_t index = 0; index < pixels.size(); ++index) {
        consumers.emplace_back([&, index]() { pixels[index] = deferred->GetData(); });
    }
    for (auto& consumer : consumers)
        consumer.join();
    CHECK(calls == 1);
    CHECK_FALSE(deferred->IsDeferred());
    for (auto* data : pixels) {
        REQUIRE(data == expected_data);
        CHECK(data[47] == 93);
    }
    CHECK(VideoFrameValid(deferred));
    CHECK(calls == 1);
    deferred.reset();
    CHECK(retained_source.expired());
}

TEST_CASE("Unused deferred host pixels release their source without conversion",
          "[video-frame-safety][deferred-host]") {
    auto owner                        = std::make_shared<int>(42);
    std::weak_ptr<int> retained_owner = owner;
    int calls                         = 0;
    auto deferred =
        std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456, [owner, &calls]() {
            (void)owner;
            ++calls;
            return VideoFramePtr{};
        });
    owner.reset();
    REQUIRE(deferred->Active());
    CHECK_FALSE(retained_owner.expired());
    deferred.reset();
    CHECK(calls == 0);
    CHECK(retained_owner.expired());
}

TEST_CASE("Deferred host failures are cached and rejected by pixel validation",
          "[video-frame-safety][deferred-host]") {
    int calls     = 0;
    auto deferred = std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456, [&]() {
        ++calls;
        return VideoFramePtr{};
    });
    REQUIRE(deferred->Active());
    CHECK_FALSE(VideoFrameValid(deferred));
    CHECK(deferred->GetData() == nullptr);
    CHECK_FALSE(deferred->Active());
    CHECK_FALSE(deferred->IsDeferred());
    CHECK(calls == 1);
}

TEST_CASE("Inference rejects failed deferred pixels without shrinking the image batch",
          "[video-frame-safety][deferred-host]") {
    auto failed     = std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456,
                                                   []() { return VideoFramePtr{}; });
    int later_calls = 0;
    auto later      = std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 43, 123457, [&]() {
        ++later_calls;
        return VideoFramePtr{};
    });
    SECTION("first host request fails") {
        REQUIRE(failed->Active());
    }
    SECTION("another consumer already observed the failure") {
        REQUIRE(failed->GetData() == nullptr);
        REQUIRE_FALSE(failed->Active());
    }
    std::vector<std::shared_ptr<nn::Blob>> blobs;
    CHECK(ConvertImagesToBlobs({failed, later}, blobs) == util::ErrorEnum::InvalidParam);
    CHECK(blobs.empty());
    CHECK(later_calls == 0);
    CHECK(ConvertImageToBlob(failed) == nullptr);
}

TEST_CASE("Deferred host frames reject a mismatched provider layout", "[video-frame-safety][deferred-host]") {
    mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {48});
    mem::SetMemoryPoolContext(&pool);
    struct ResetPool {
        ~ResetPool() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } reset_pool;
    auto rgb = std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_RGB8);
    REQUIRE(rgb->Active());
    auto deferred =
        std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456, [rgb]() { return rgb; });
    CHECK_FALSE(VideoFrameValid(deferred));
    CHECK(deferred->GetData() == nullptr);
}

TEST_CASE("Deferred host exceptions are cached across concurrent consumers",
          "[video-frame-safety][deferred-host]") {
    std::atomic<int> calls{0};
    auto deferred =
        std::make_shared<VideoFrame>(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456, [&]() -> VideoFramePtr {
            ++calls;
            throw std::runtime_error("conversion failed");
        });
    std::array<bool, 8> valid{};
    std::vector<std::thread> consumers;
    for (size_t index = 0; index < valid.size(); ++index)
        consumers.emplace_back([&, index]() { valid[index] = VideoFrameValid(deferred); });
    for (auto& consumer : consumers)
        consumer.join();
    CHECK(calls == 1);
    CHECK(std::none_of(valid.begin(), valid.end(), [](bool value) { return value; }));
    CHECK_FALSE(deferred->Active());
    CHECK_FALSE(deferred->IsDeferred());
}

TEST_CASE("Moving a deferred host frame preserves identity without conversion",
          "[video-frame-safety][deferred-host]") {
    int calls = 0;
    VideoFrame source(4, 4, PixelFormat::PIXEL_BGR8, 42, 123456, [&]() {
        ++calls;
        return VideoFramePtr{};
    });
    source.SetStreamIndex(7);
    VideoFrame target(0, 0);
    target = std::move(source);
    CHECK_FALSE(source.Active());
    CHECK(target.Active());
    CHECK(target.IsDeferred());
    CHECK(target.GetFrameIndex() == 42);
    CHECK(target.GetTimestamp() == 123456);
    CHECK(target.GetStreamIndex() == 7);
    CHECK(calls == 0);
    CHECK(target.GetData() == nullptr);
    CHECK(calls == 1);
}

TEST_CASE("Frame size calculation rejects unsafe dimensions", "[video-frame-safety]") {
    REQUIRE_FALSE(PixelFormatUtils::CalculateFrameSize(-1, 1080, PixelFormat::PIXEL_I420));
    REQUIRE_FALSE(PixelFormatUtils::CalculateFrameSize(1920, 0, PixelFormat::PIXEL_I420));
    REQUIRE_FALSE(PixelFormatUtils::CalculateFrameSize(1920, 1080, PixelFormat::PIXEL_UNKNOWN));
    REQUIRE_FALSE(PixelFormatUtils::CalculateFrameSize(1919, 1080, PixelFormat::PIXEL_I420));
    REQUIRE_FALSE(PixelFormatUtils::CalculateFrameSize(1920, 1079, PixelFormat::PIXEL_NV12));
    REQUIRE_FALSE(PixelFormatUtils::CalculateFrameSize(1919, 1080, PixelFormat::PIXEL_YUYV));
    REQUIRE_FALSE(PixelFormatUtils::CalculateFrameSize(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), PixelFormat::PIXEL_RGB32F));

    const auto max_rgb_size =
        PixelFormatUtils::CalculateFrameSize(kVideoMaxWidth, kVideoMaxHeight, PixelFormat::PIXEL_RGB8);
    REQUIRE(max_rgb_size);
    REQUIRE(*max_rgb_size == static_cast<size_t>(kVideoFrameMaxSize));
    REQUIRE_FALSE(
        PixelFormatUtils::CalculateFrameSize(kVideoMaxWidth, kVideoMaxHeight, PixelFormat::PIXEL_RGBA8));

    const auto i420_size = PixelFormatUtils::CalculateFrameSize(1920, 1080, PixelFormat::PIXEL_I420);
    REQUIRE(i420_size);
    REQUIRE(*i420_size == 3110400);
}

TEST_CASE("Encoded image headers are checked before decoded-frame allocation",
          "[video-frame-safety][image]") {
    const auto one_pixel_png = util::DecBase64Vec(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
        "x8AAusB9Y9Zl1sAAAAASUVORK5CYII=");
    EncodedImageInfo info;
    REQUIRE(InspectEncodedImage(one_pixel_png, info));
    CHECK(info.width == 1);
    CHECK(info.height == 1);
    CHECK(info.pixel_count == 1);
    CHECK(IsEncodedImageWithinFrameCapability(info));

    std::vector<std::uint8_t> oversized_bmp(54, 0);
    oversized_bmp[0]  = 'B';
    oversized_bmp[1]  = 'M';
    oversized_bmp[2]  = 54;
    oversized_bmp[10] = 54;
    oversized_bmp[14] = 40;
    // BITMAPINFOHEADER width=8000, height=8000, planes=1, bpp=24.
    oversized_bmp[18] = 0x40;
    oversized_bmp[19] = 0x1f;
    oversized_bmp[22] = 0x40;
    oversized_bmp[23] = 0x1f;
    oversized_bmp[26] = 1;
    oversized_bmp[28] = 24;

    REQUIRE(InspectEncodedImage(oversized_bmp, info));
    CHECK(info.pixel_count == 64'000'000);
    CHECK_FALSE(IsEncodedImageWithinFrameCapability(info));
    CHECK(info.pixel_count > MaxDecodedImagePixels());

    CHECK_FALSE(InspectEncodedImage({0, 1, 2, 3}, info));
}

TEST_CASE("VideoFrame rejects unsafe dimensions before allocation", "[video-frame-safety]") {
    VideoFrame negative_width(-1, 1080, PixelFormat::PIXEL_I420);
    REQUIRE_FALSE(negative_width.Active());
    REQUIRE(negative_width.GetWidth() == 0);
    REQUIRE(negative_width.GetHeight() == 0);
    REQUIRE(negative_width.GetSize() == 0);

    VideoFrame oversized(kVideoMaxWidth, kVideoMaxHeight, PixelFormat::PIXEL_RGBA8);
    REQUIRE_FALSE(oversized.Active());
    REQUIRE(oversized.GetWidth() == 0);
    REQUIRE(oversized.GetHeight() == 0);
    REQUIRE(oversized.GetSize() == 0);
}

TEST_CASE("Crop ROI normalization safely clips and aligns recoverable input", "[video-frame-safety]") {
    constexpr int kMinDimension = 16;
    constexpr int kMaxDimension = 8192;
    const auto normalize        = [=](const util::Box& roi) {
        return PixelFormatUtils::NormalizeCropRoi(64, 64, PixelFormat::PIXEL_I420, roi, kMinDimension,
                                                         kMaxDimension);
    };

    REQUIRE(normalize({-1, 0, 16, 16}) == util::Box(0, 0, 16, 16));
    REQUIRE(normalize({0, -1, 16, 16}) == util::Box(0, 0, 16, 16));
    REQUIRE(normalize({0, 0, 6, 16}) == util::Box(0, 0, 16, 16));
    REQUIRE(normalize({0, 0, 15, 15}) == util::Box(0, 0, 16, 16));
    REQUIRE(normalize({30, 30, 1, 1}) == util::Box(23, 23, 16, 16));
    REQUIRE(normalize({60, 0, 8, 8}) == util::Box(48, 0, 16, 16));
    REQUIRE(normalize({0, 60, 8, 8}) == util::Box(0, 48, 16, 16));
    REQUIRE_FALSE(normalize({0, 0, 0, 16}));
    REQUIRE_FALSE(normalize({64, 0, 16, 16}));
    REQUIRE_FALSE(normalize({std::numeric_limits<int>::max(), 0, 16, 16}));
    REQUIRE_FALSE(normalize({1, 1, std::numeric_limits<int>::max(), 16}));
}

#ifdef COSMO_NN_USE_SOPHON_BACKEND
namespace {

    class StubOsdTextRenderer final : public IOsdTextRenderer {
    public:
        bool Init(const std::string&) override {
            return true;
        }

        bool IsReady() const override {
            return true;
        }

        TextBitmap RenderString(const std::string&, float) const override {
            return {};
        }

        OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
            return {};
        }
    };

    class SophonMediaFixture {
    public:
        SophonMediaFixture()
            : device_context_(std::make_unique<mem::DeviceContext>()),
              device_context_registration_(*device_context_) {
            memory_pool_ = std::make_unique<service::MemoryPoolServiceImpl>();
            frame_proc_  = std::make_unique<VideoFrameProcSophon>(device_context_->GetMediaHandle(), osd_);
        }

        SophonMediaFixture(const SophonMediaFixture&)            = delete;
        SophonMediaFixture& operator=(const SophonMediaFixture&) = delete;

        VideoFrameProcSophon& FrameProc() {
            return *frame_proc_;
        }

        void* MediaHandle() {
            return device_context_->GetMediaHandle();
        }

    private:
        StubOsdTextRenderer osd_;
        std::unique_ptr<mem::DeviceContext> device_context_;
        test::ScopedServiceOverride<mem::IDeviceContext> device_context_registration_;
        std::unique_ptr<service::MemoryPoolServiceImpl> memory_pool_;
        std::unique_ptr<VideoFrameProcSophon> frame_proc_;
    };

}  // namespace

TEST_CASE("Sophon crop rejects unsafe ROIs before VPP", "[sophon-crop][.device]") {
    SophonMediaFixture fixture;
    auto source = std::make_shared<VideoFrame>(64, 64, PixelFormat::PIXEL_I420, 42, 123456);
    REQUIRE(VideoFrameValid(source, true));

    const util::Box invalid_rois[] = {
        {0, 0, 0, 16},
        {0, 0, 16, -2},
        {64, 0, 16, 16},
        {0, 64, 16, 16},
        {std::numeric_limits<int>::max(), 0, 16, 16},
        {0, std::numeric_limits<int>::max(), 16, 16},
        {1, 1, std::numeric_limits<int>::max(), 16},
        {1, 1, 16, std::numeric_limits<int>::max()},
    };

    for (const auto& roi : invalid_rois) {
        INFO("ROI x=" << roi.x << " y=" << roi.y << " width=" << roi.width << " height=" << roi.height);
        REQUIRE(fixture.FrameProc().Crop(source, roi) == nullptr);
    }
}

TEST_CASE("Sophon crop normalizes recoverable ROIs", "[sophon-crop][.device]") {
    SophonMediaFixture fixture;
    auto source = std::make_shared<VideoFrame>(64, 64, PixelFormat::PIXEL_I420, 42, 123456);
    REQUIRE(VideoFrameValid(source, true));

    struct CropCase {
        util::Box roi;
        size_t expected_width;
        size_t expected_height;
    };
    const CropCase crop_cases[] = {
        {{1, 3, 15, 15}, 16, 16},
        {{60, 60, 8, 8}, 16, 16},
        {{-1, 0, 16, 16}, 16, 16},
        {{48, 48, 16, 16}, 16, 16},
    };

    for (const auto& crop_case : crop_cases) {
        INFO("ROI x=" << crop_case.roi.x << " y=" << crop_case.roi.y << " width=" << crop_case.roi.width
                      << " height=" << crop_case.roi.height);
        auto result = fixture.FrameProc().Crop(source, crop_case.roi);
        REQUIRE(VideoFrameValid(result, true));
        REQUIRE(result->GetWidth() == crop_case.expected_width);
        REQUIRE(result->GetHeight() == crop_case.expected_height);
        REQUIRE(result->GetFrameIndex() == source->GetFrameIndex());
        REQUIRE(result->GetTimestamp() == source->GetTimestamp());
    }
}

TEST_CASE("Sophon encoder survives startup without immediate output", "[sophon-encoder][.device]") {
    constexpr int kWidth  = 1280;
    constexpr int kHeight = 720;

    SophonMediaFixture fixture;
    auto encoder = VideoEncoder::Create(fixture.MediaHandle());
    REQUIRE(encoder);
    encoder->Set(VideoCodecType::kH264, kWidth, kHeight);
    REQUIRE(encoder->Open());

    auto frame = std::make_shared<VideoFrame>(kWidth, kHeight, PixelFormat::PIXEL_I420);
    REQUIRE(VideoFrameValid(frame, true));
    auto* device_memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
    REQUIRE(device_memory != nullptr);

    std::vector<uint8_t> gray_frame(frame->GetSize(), 128);
    std::fill_n(gray_frame.begin(), static_cast<size_t>(kWidth) * kHeight, 16);
    REQUIRE(bm_memcpy_s2d_partial(reinterpret_cast<bm_handle_t>(fixture.MediaHandle()), *device_memory,
                                  gray_frame.data(),
                                  static_cast<unsigned int>(gray_frame.size())) == BM_SUCCESS);

    VideoPacketPtr last_packet;
    size_t packet_count                   = 0;
    size_t last_output_frame_index        = 0;
    size_t submitted_frame_count          = 0;
    constexpr size_t kExpectedPacketCount = 3;
    constexpr size_t kFrameCount          = 64;
    const auto deadline                   = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (size_t frame_index = 0; frame_index < kFrameCount; ++frame_index) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        frame->SetFrameIndex(frame_index);
        frame->SetTimestamp(static_cast<int64_t>(frame_index) * 40);
        auto packet = encoder->Encode(frame);
        ++submitted_frame_count;
        if (packet) {
            REQUIRE_FALSE(packet->data.empty());
            last_packet             = std::move(packet);
            last_output_frame_index = frame_index;
            ++packet_count;
        }
    }

    REQUIRE(submitted_frame_count == kFrameCount);
    REQUIRE(packet_count >= kExpectedPacketCount);
    REQUIRE(last_output_frame_index >= (kFrameCount * 3) / 4);
    REQUIRE(last_packet);
    REQUIRE(last_packet->width == kWidth);
    REQUIRE(last_packet->height == kHeight);
    REQUIRE(last_packet->codec_type == VideoCodecType::kH264);
}
#endif

}  // namespace cosmo::media
