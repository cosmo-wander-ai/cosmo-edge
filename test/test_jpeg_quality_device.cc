#include "catch_amalgamated.hpp"

#ifdef COSMO_MEDIA_USE_SOPHON_BACKEND
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "bmlib_runtime.h"
#include "media/EncodedImageInfo.h"
#include "media/IOsdTextRenderer.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorSophon.h"
#include "mem/DeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "service/system/impl/SystemServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"

namespace {

// The test runs from a private run directory. mkdtemp creates a unique 0700
// child; ScopedPathOverride prevents any access to the installed configuration.
class JpegConfigDirectory {
public:
    JpegConfigDirectory() {
        char name[]         = "cosmo-jpeg-quality-XXXXXX";
        const auto* created = ::mkdtemp(name);
        if (!created) {
            throw std::runtime_error("cannot create private JPEG test directory");
        }
        path = std::filesystem::absolute(created);
    }
    ~JpegConfigDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

class JpegTextRenderer final : public cosmo::media::IOsdTextRenderer {
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

class JpegDeviceFixture {
public:
    JpegDeviceFixture()
        : device_registration_(device_),
          renderer_registration_(renderer_),
          pool_(std::make_unique<cosmo::mem::AllocatorSophon>(device_), {128 * 96 * 3}) {
        cosmo::mem::SetMemoryPoolContext(&pool_);
    }
    ~JpegDeviceFixture() {
        cosmo::mem::SetMemoryPoolContext(nullptr);
    }

    void Upload(const VideoFramePtr& frame, std::vector<uint8_t>& pixels) {
        REQUIRE(frame);
        REQUIRE(frame->Active());
        REQUIRE(frame->GetSize() == pixels.size());
        auto* memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
        REQUIRE(memory != nullptr);
        REQUIRE(bm_memcpy_s2d_partial(reinterpret_cast<bm_handle_t>(device_.GetMediaHandle()), *memory,
                                      pixels.data(), static_cast<unsigned int>(pixels.size())) == BM_SUCCESS);
    }

    cosmo::service::VideoFrameServiceImpl& Video() {
        return video_;
    }

private:
    cosmo::mem::DeviceContext device_;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> device_registration_;
    JpegTextRenderer renderer_;
    cosmo::test::ScopedServiceOverride<cosmo::media::IOsdTextRenderer> renderer_registration_;
    cosmo::mem::MemoryPoolMng pool_;
    cosmo::service::VideoFrameServiceImpl video_;
};

// Inspect actual JPEG quantization values, not just the request or file size.
// stb emits 8-bit DQT tables; all reads are bounded by the marker's segment.
std::vector<uint8_t> QuantizationValues(const std::vector<uint8_t>& jpeg) {
    REQUIRE(jpeg.size() >= 4);
    REQUIRE(jpeg[0] == 0xff);
    REQUIRE(jpeg[1] == 0xd8);
    std::vector<uint8_t> values;
    size_t offset = 2;
    while (offset < jpeg.size()) {
        REQUIRE(jpeg[offset] == 0xff);
        while (offset < jpeg.size() && jpeg[offset] == 0xff) {
            ++offset;
        }
        REQUIRE(offset < jpeg.size());
        const auto marker = jpeg[offset++];
        if (marker == 0xda || marker == 0xd9) {
            break;
        }
        REQUIRE(offset + 2 <= jpeg.size());
        const size_t length = (static_cast<size_t>(jpeg[offset]) << 8) | jpeg[offset + 1];
        REQUIRE(length >= 2);
        REQUIRE(length <= jpeg.size() - offset);
        const size_t end = offset + length;
        offset += 2;
        if (marker == 0xdb) {
            while (offset < end) {
                const auto selector = jpeg[offset++];
                REQUIRE((selector >> 4) == 0);
                REQUIRE(end - offset >= 64);
                values.insert(values.end(), jpeg.begin() + offset, jpeg.begin() + offset + 64);
                offset += 64;
            }
        }
        offset = end;
    }
    REQUIRE_FALSE(values.empty());
    return values;
}

}  // namespace

TEST_CASE("Sophon JPEG follows persisted picture quality changes on the same frame",
          "[.device][jpeg-quality-device]") {
    JpegConfigDirectory directory;
    cosmo::test::ScopedPathOverride paths(directory.path.string(), directory.path.string());
    const auto config_path = directory.path / "conf";
    REQUIRE(std::filesystem::create_directory(config_path));
    {
        std::ofstream alarm(config_path / "alarmParam.json");
        alarm << R"({"overviewInfo":{"picQuality":75},"videoRecordInfo":{}})";
        REQUIRE(alarm.good());
        std::ofstream reboot(config_path / "devRebootParam.json");
        reboot << R"({"isTimingRestart":false,"weekDay":0,"restartTimeSec":7200})";
        REQUIRE(reboot.good());
        std::ofstream system(config_path / "devSystemParam.json");
        system << "{}";
        REQUIRE(system.good());
    }
    cosmo::service::SystemServiceImpl config;
    cosmo::test::ScopedServiceOverride<cosmo::service::IConfigReadService> config_registration(config);
    JpegDeviceFixture fixture;
    constexpr int width  = 128;
    constexpr int height = 96;
    auto format          = cosmo::media::PixelFormat::PIXEL_RGB8;
    SECTION("RGB input") {
        format = cosmo::media::PixelFormat::PIXEL_RGB8;
    }
    SECTION("I420 input exercises device color conversion") {
        format = cosmo::media::PixelFormat::PIXEL_I420;
    }
    INFO("Source pixel format: " << (format == cosmo::media::PixelFormat::PIXEL_I420 ? "I420" : "RGB"));
    auto frame = std::make_shared<cosmo::media::VideoFrame>(width, height, format);
    REQUIRE(frame->Active());
    std::vector<uint8_t> pixels(frame->GetSize());
    if (format == cosmo::media::PixelFormat::PIXEL_I420) {
        constexpr size_t luma_size   = width * height;
        constexpr size_t chroma_size = (width / 2) * (height / 2);
        REQUIRE(pixels.size() == luma_size + 2 * chroma_size);
        // Planar Y, U, V with chroma at half resolution and limited-range values.
        // This reaches Sophon's non-RGB storage conversion before JPEG encoding.
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                pixels[static_cast<size_t>(y * width + x)] =
                    static_cast<uint8_t>(16 + (x * 17 + y * 13) % 220);
            }
        }
        for (int y = 0; y < height / 2; ++y) {
            for (int x = 0; x < width / 2; ++x) {
                const auto offset                        = static_cast<size_t>(y * (width / 2) + x);
                pixels[luma_size + offset]               = static_cast<uint8_t>(64 + (x * 7 + y * 29) % 128);
                pixels[luma_size + chroma_size + offset] = static_cast<uint8_t>(64 + (x * 31 + y * 3) % 128);
            }
        }
    } else {
        REQUIRE(pixels.size() == width * height * 3);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto offset  = static_cast<size_t>((y * width + x) * 3);
                pixels[offset]     = static_cast<uint8_t>((x * 17 + y * 13) % 256);
                pixels[offset + 1] = static_cast<uint8_t>((x * 7 + y * 29) % 256);
                pixels[offset + 2] = static_cast<uint8_t>((x * 31 + y * 3) % 256);
            }
        }
    }
    fixture.Upload(frame, pixels);

    const auto encode = [&](int quality) {
        auto settings       = config.GetPictureQuality();
        settings.picQuality = quality;
        REQUIRE(config.SetPictureQuality(settings) == cosmo::util::ErrorEnum::Success);
        // Reload the real persisted config; no running service or business data is touched.
        cosmo::service::SystemServiceImpl reloaded;
        REQUIRE(reloaded.GetPictureQuality().picQuality == quality);
        auto& reader = cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IConfigReadService>();
        REQUIRE(reader.GetPictureQuality().picQuality == quality);
        // This is the same config-read -> codec call used by TaskAlarmPicture.
        // It validates that boundary without emitting a business alarm.
        auto jpeg = fixture.Video().EncodeJpeg(frame, reader.GetPictureQuality().picQuality);
        REQUIRE_FALSE(jpeg.empty());
        cosmo::media::EncodedImageInfo info;
        REQUIRE(cosmo::media::InspectEncodedImage(jpeg, info));
        REQUIRE(info.width == width);
        REQUIRE(info.height == height);
        return jpeg;
    };

    const auto low           = encode(20);
    const auto high          = encode(95);
    const auto low_again     = encode(20);
    const auto low_dqt       = QuantizationValues(low);
    const auto high_dqt      = QuantizationValues(high);
    const auto low_again_dqt = QuantizationValues(low_again);
    INFO("JPEG bytes at quality 20/95/20: " << low.size() << "/" << high.size() << "/" << low_again.size());
    CHECK(low.size() < high.size());
    CHECK(low == low_again);
    CHECK(low_dqt.size() == high_dqt.size());
    CHECK(low_dqt != high_dqt);
    CHECK(low_dqt == low_again_dqt);
    const auto low_sum  = std::accumulate(low_dqt.begin(), low_dqt.end(), 0u);
    const auto high_sum = std::accumulate(high_dqt.begin(), high_dqt.end(), 0u);
    INFO("DQT sums at quality 20/95: " << low_sum << "/" << high_sum);
    CHECK(low_sum > high_sum);
}
#endif
