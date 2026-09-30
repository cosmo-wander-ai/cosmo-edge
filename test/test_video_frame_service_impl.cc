#include "catch_amalgamated.hpp"
/*
 * test_video_frame_service_impl.cc — VideoFrameServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: VideoFrameServiceImpl wraps VPU image processing (bmcv).
 * Construction creates VideoFrameProc which requires bmlib device init.
 * All tests tagged [.device].
 */
#include <memory>
#include <stdexcept>
#include <string>

#include "media/IOsdTextRenderer.h"
#include "mem/DeviceContext.h"
#include "mem/IDeviceContext.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "support/ScopedServiceOverride.h"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include <algorithm>
#include <chrono>
#include <future>

extern "C" {
#include <libavutil/cpu.h>
}

#include "media/IOsdTextRenderer.h"
#include "media/PixelFormat.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/detail/ServiceRegistry.h"
#endif

using namespace cosmo::service;

namespace {

class StubOsdTextRenderer final : public cosmo::media::IOsdTextRenderer {
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

class ScopedDeviceContext {
public:
    ScopedDeviceContext()
        : device_context_(std::make_unique<cosmo::mem::DeviceContext>()), registration_(*device_context_) {}

    ScopedDeviceContext(const ScopedDeviceContext&)            = delete;
    ScopedDeviceContext& operator=(const ScopedDeviceContext&) = delete;

private:
    std::unique_ptr<cosmo::mem::DeviceContext> device_context_;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> registration_;
};

class VideoFrameServiceFixture {
public:
    VideoFrameServiceFixture() : osd_registration_(osd_) {}

    VideoFrameServiceFixture(const VideoFrameServiceFixture&)            = delete;
    VideoFrameServiceFixture& operator=(const VideoFrameServiceFixture&) = delete;

    VideoFrameServiceImpl& Service() {
        return service_;
    }

private:
    ScopedDeviceContext device_context_;
    StubOsdTextRenderer osd_;
    cosmo::test::ScopedServiceOverride<cosmo::media::IOsdTextRenderer> osd_registration_;
    VideoFrameServiceImpl service_;
};

}  // namespace

#ifdef COSMO_MEDIA_USE_CPU_BACKEND
TEST_CASE("CPU scalar preview resize preserves neutral I420 planes", "[VideoFrameService][.cpu-scalar]") {
    // Run in a fresh process with COSMO_FFMPEG_DISABLE_CPU_OPT=1.
    VideoFrameServiceFixture fixture;
    REQUIRE(av_get_cpu_flags() == 0);
    constexpr int width         = 3040;
    constexpr int height        = 1368;
    constexpr int output_width  = 1920;
    constexpr int output_height = 1088;
    constexpr int source_size   = width * height * 3 / 2;
    constexpr int output_size   = output_width * output_height * 3 / 2;
    cosmo::mem::MemoryPoolMng memory_pool(std::make_unique<cosmo::mem::AllocatorCpu>(),
                                          {source_size, output_size});
    cosmo::mem::SetMemoryPoolContext(&memory_pool);
    struct ContextReset {
        ~ContextReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } context_reset;
    auto frame =
        std::make_shared<cosmo::media::VideoFrame>(width, height, cosmo::media::PixelFormat::PIXEL_I420);
    REQUIRE(frame->Active());
    std::fill_n(frame->GetData(), width * height, 100);
    std::fill_n(frame->GetData() + width * height, width * height / 2, 128);
    auto output = fixture.Service().Resize(frame, output_height, output_width);
    REQUIRE(output);
    REQUIRE(output->Active());
    REQUIRE(output->GetWidth() == output_width);
    REQUIRE(output->GetHeight() == output_height);
    auto* data       = output->GetData();
    const int y_size = output_width * output_height;
    CHECK(std::all_of(data, data + y_size, [](uint8_t value) { return value == 100; }));
    CHECK(std::all_of(data + y_size, data + y_size * 5 / 4, [](uint8_t value) { return value == 128; }));
    CHECK(std::all_of(data + y_size * 5 / 4, data + output_size, [](uint8_t value) { return value == 128; }));
}
#endif

TEST_CASE("VideoFrameServiceImpl: construction and destruction", "[VideoFrameService][.device]") {
    REQUIRE_NOTHROW([]() { VideoFrameServiceFixture fixture; }());
}

TEST_CASE("VideoFrameServiceImpl: EncodeJpeg with null frame returns empty", "[VideoFrameService][.device]") {
    VideoFrameServiceFixture fixture;
    auto& sut = fixture.Service();
    VideoFramePtr nullFrame;
    auto data = sut.EncodeJpeg(nullFrame);
    REQUIRE(data.empty());
}

TEST_CASE("VideoFrameServiceImpl: DecodeJpeg with empty data returns null", "[VideoFrameService][.device]") {
    VideoFrameServiceFixture fixture;
    auto& sut = fixture.Service();
    std::vector<uint8_t> emptyData;
    auto frame = sut.DecodeJpeg(emptyData);
    REQUIRE(frame == nullptr);
}

TEST_CASE("VideoFrameServiceImpl: EnsureHostData with null frame", "[VideoFrameService][.device]") {
    VideoFrameServiceFixture fixture;
    auto& sut = fixture.Service();
    VideoFramePtr nullFrame;
    REQUIRE(sut.EnsureHostData(nullFrame) == false);
}

TEST_CASE("VideoFrameServiceImpl: Crop with null frame", "[VideoFrameService][.device]") {
    VideoFrameServiceFixture fixture;
    auto& sut = fixture.Service();
    VideoFramePtr nullFrame;
    cosmo::util::Box roi{0, 0, 100, 100};
    auto result = sut.Crop(nullFrame, roi);
    REQUIRE(result == nullptr);
}

TEST_CASE("VideoFrameServiceImpl serializes complete concurrent OSD sessions",
          "[VideoFrameService][osd][concurrency]") {
#if !defined(COSMO_MEDIA_USE_CPU_BACKEND)
    SKIP("host-backed frame processor required");
#else
    using namespace std::chrono_literals;

    class TestDeviceContext final : public cosmo::mem::IDeviceContext {
    public:
        void* GetMemoryHandle() override {
            return nullptr;
        }
        void* GetMediaHandle() override {
            return nullptr;
        }
    } device_context;

    class TestTextRenderer final : public cosmo::media::IOsdTextRenderer {
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
    } text_renderer;

    constexpr int width        = 64;
    constexpr int height       = 64;
    constexpr size_t frameSize = static_cast<size_t>(width) * height * 3 / 2;
    cosmo::mem::MemoryPoolMng memory_pool(std::make_unique<cosmo::mem::AllocatorCpu>(),
                                          {static_cast<int>(frameSize)});
    cosmo::mem::SetMemoryPoolContext(&memory_pool);
    struct ContextReset {
        ~ContextReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } context_reset;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> device_registration(device_context);
    cosmo::test::ScopedServiceOverride<cosmo::media::IOsdTextRenderer> text_registration(text_renderer);

    VideoFrameServiceImpl sut;
    auto first =
        std::make_shared<cosmo::media::VideoFrame>(width, height, cosmo::media::PixelFormat::PIXEL_I420);
    auto second =
        std::make_shared<cosmo::media::VideoFrame>(width, height, cosmo::media::PixelFormat::PIXEL_I420);
    REQUIRE(first->Active());
    REQUIRE(second->Active());
    REQUIRE(sut.BeginOSD(first));

    auto competing_session = std::async(std::launch::async, [&]() {
        const bool started = sut.BeginOSD(second);
        if (started) {
            sut.EndOSD();
        }
        return started;
    });

    CHECK(competing_session.wait_for(100ms) == std::future_status::timeout);
    sut.EndOSD();
    REQUIRE(competing_session.wait_for(2s) == std::future_status::ready);
    CHECK(competing_session.get());
#endif
}
