#include "catch_amalgamated.hpp"

#ifdef COSMO_MEDIA_USE_SOPHON_BACKEND
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "bmlib_runtime.h"
#include "media/Color.h"
#include "media/EncodedImageInfo.h"
#include "media/IOsdTextRenderer.h"
#include "media/OsdTextRenderer.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorSophon.h"
#include "mem/DeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "support/ScopedServiceOverride.h"

namespace {
constexpr int kWidth = 1280, kHeight = 720;

// The production pool requests blocks in batches. Keep the isolated 1080p
// test bounded while still exercising the normal VideoFrame allocation path.
class CappedSophonAllocator final : public cosmo::mem::Allocator {
public:
    CappedSophonAllocator(cosmo::mem::IDeviceContext& device, int limit)
        : allocator(device), maxBlocks(limit) {}
    cosmo::mem::Block* Allocate(size_t size) override {
        int current = allocated.load();
        while (current < maxBlocks) {
            if (allocated.compare_exchange_weak(current, current + 1)) {
                auto* block = allocator.Allocate(size);
                if (!block)
                    --allocated;
                return block;
            }
        }
        return nullptr;  // Deliberate test-only cap, not a device admission result.
    }
    void Free(cosmo::mem::Block* block) override {
        if (block) {
            allocator.Free(block);
            --allocated;
        }
    }

private:
    cosmo::mem::AllocatorSophon allocator;
    const int maxBlocks;
    std::atomic<int> allocated{0};
};

class ThrowingRenderer final : public cosmo::media::IOsdTextRenderer {
public:
    bool Init(const std::string&) override {
        return true;
    }
    bool IsReady() const override {
        return true;
    }
    TextBitmap RenderString(const std::string&, float) const override {
        throw std::runtime_error("test renderer failure");
    }
    OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
        throw std::runtime_error("test renderer failure");
    }
};

std::unique_ptr<cosmo::mem::Allocator> MakeAllocator(cosmo::mem::IDeviceContext& device, int maxBlocks) {
    if (maxBlocks > 0)
        return std::make_unique<CappedSophonAllocator>(device, maxBlocks);
    return std::make_unique<cosmo::mem::AllocatorSophon>(device);
}

class SophonLineFixture {
public:
    explicit SophonLineFixture(const std::string& font, int frameWidth = kWidth, int frameHeight = kHeight,
                               int maxBlocks = 0, cosmo::media::IOsdTextRenderer* customRenderer = nullptr)
        : width(frameWidth),
          height(frameHeight),
          deviceRegistration(device),
          rendererRegistration(customRenderer ? *customRenderer : renderer),
          pool(MakeAllocator(device, maxBlocks), {frameWidth * frameHeight * 3}) {
        if (!font.empty() && !customRenderer)
            REQUIRE(renderer.Init(font));
        cosmo::mem::SetMemoryPoolContext(&pool);
    }
    ~SophonLineFixture() {
        cosmo::mem::SetMemoryPoolContext(nullptr);
    }
    VideoFramePtr Frame(bool textured = false) {
        auto frame =
            std::make_shared<cosmo::media::VideoFrame>(width, height, cosmo::media::PixelFormat::PIXEL_I420);
        REQUIRE(VideoFrameValid(frame));
        std::vector<uint8_t> pixels(frame->GetSize(), 128);
        if (textured) {
            // A deterministic light/dark surveillance-like test field, not an
            // actual captured scene and not an inference result.
            for (int row = 0; row < height; ++row) {
                for (int x = 0; x < width; ++x) {
                    const int grid = ((x / 160) + (row / 120)) % 2;
                    pixels[static_cast<size_t>(row) * width + x] =
                        static_cast<uint8_t>(grid ? (160 + (x % 32)) : (42 + (row % 48)));
                }
            }
        }
        Upload(frame, pixels);
        return frame;
    }
    void Upload(const VideoFramePtr& frame, std::vector<uint8_t>& pixels) {
        REQUIRE(pixels.size() == frame->GetSize());
        auto* memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
        REQUIRE(memory != nullptr);
        REQUIRE(bm_memcpy_s2d_partial(reinterpret_cast<bm_handle_t>(device.GetMediaHandle()), *memory,
                                      pixels.data(), static_cast<unsigned int>(pixels.size())) == BM_SUCCESS);
    }
    std::vector<uint8_t> Download(const VideoFramePtr& frame) {
        std::vector<uint8_t> pixels(frame->GetSize());
        auto* memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
        REQUIRE(memory != nullptr);
        REQUIRE(bm_memcpy_d2s_partial(reinterpret_cast<bm_handle_t>(device.GetMediaHandle()), pixels.data(),
                                      *memory, static_cast<unsigned int>(pixels.size())) == BM_SUCCESS);
        return pixels;
    }
    cosmo::service::VideoFrameServiceImpl& Video() {
        return video;
    }

private:
    int width;
    int height;
    cosmo::mem::DeviceContext device;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> deviceRegistration;
    cosmo::media::OsdTextRenderer renderer;
    cosmo::test::ScopedServiceOverride<cosmo::media::IOsdTextRenderer> rendererRegistration;
    cosmo::mem::MemoryPoolMng pool;
    cosmo::service::VideoFrameServiceImpl video;
};

void SaveJpeg(SophonLineFixture& fixture, const VideoFramePtr& frame, const std::filesystem::path& path) {
    const auto jpeg = fixture.Video().EncodeJpeg(frame, 95);
    REQUIRE_FALSE(jpeg.empty());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
    out.close();
    REQUIRE(out.good());
}
}  // namespace

TEST_CASE("Sophon CPU OSD composites lines and text in complete sessions", "[.device][sophon-cpu-lines]") {
    const auto* enabled = std::getenv("COSMO_RUN_SOPHON_CPU_LINES");
    if (!enabled || std::string(enabled) != "1")
        SKIP("Set COSMO_RUN_SOPHON_CPU_LINES=1 with a private artifact directory and font path");
    const auto* output = std::getenv("COSMO_SOPHON_CPU_LINES_OUTPUT_DIR");
    const auto* font   = std::getenv("COSMO_ALARM_ANNOTATION_FONT");
    REQUIRE(output != nullptr);
    REQUIRE(font != nullptr);
    const std::filesystem::path parent(output);
    REQUIRE(parent.is_absolute());
    REQUIRE(std::filesystem::is_directory(parent));
    const auto permissions = std::filesystem::status(parent).permissions();
    REQUIRE((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
            std::filesystem::perms::none);
    REQUIRE(std::filesystem::is_regular_file(font));
    auto pattern = (parent / "cpu-lines-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    const auto* created = ::mkdtemp(name.data());
    REQUIRE(created != nullptr);
    const std::filesystem::path directory(created);

    SECTION("Lines only, including more than 128 segments") {
        SophonLineFixture fixture(font);
        auto frame = fixture.Frame();
        std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines;
        for (int i = 0; i < 150; ++i)
            lines.push_back({{20, 20 + i * 4}, {400, 20 + i * 4}});
        REQUIRE(fixture.Video().BeginOSD(frame));
        fixture.Video().OSDDrawLines(lines, {168, 164, 255}, 2);
        fixture.Video().EndOSD();
        const auto pixels = fixture.Download(frame);
        CHECK(pixels[static_cast<size_t>(20) * kWidth + 100] != 128);
        CHECK(pixels[static_cast<size_t>(20 + 149 * 4) * kWidth + 100] != 128);
        CHECK(pixels[static_cast<size_t>(10) * kWidth + 100] == 128);
        SaveJpeg(fixture, frame, directory / "lines-only.jpg");
    }
    SECTION("Lines precede labels in one complete session") {
        SophonLineFixture fixture(font);
        auto frame = fixture.Frame();
        REQUIRE(fixture.Video().BeginOSD(frame));
        fixture.Video().OSDDrawLines({{{100, 100}, {800, 100}},
                                      {{800, 100}, {800, 450}},
                                      {{800, 450}, {100, 450}},
                                      {{100, 450}, {100, 100}}},
                                     {255, 107, 107}, 2);
        fixture.Video().OSDDrawTextEx(400, 100, "TEST 92.0%", {255, 255, 255}, 20, {0, 0, 0}, 255, true, 4);
        fixture.Video().EndOSD();
        const auto pixels = fixture.Download(frame);
        CHECK(pixels[static_cast<size_t>(100) * kWidth + 200] != 128);
        CHECK(pixels[static_cast<size_t>(100) * kWidth + 395] == 0);  // Text background covers the line.
        SaveJpeg(fixture, frame, directory / "lines-and-text.jpg");
    }
    SECTION("Compact corner brackets and unified label remain inside the frame") {
        SophonLineFixture fixture(font);
        auto frame        = fixture.Frame(true);
        const auto before = fixture.Download(frame);
        REQUIRE(fixture.Video().BeginOSD(frame));
        // The same source-pixel primitives, colors and text sizes used by the
        // live target OSD. The pattern is synthetic, not a detection result.
        fixture.Video().OSDDrawLines({{{320, 270}, {340, 270}},
                                      {{320, 270}, {320, 290}},
                                      {{500, 270}, {480, 270}},
                                      {{500, 270}, {500, 290}},
                                      {{320, 510}, {340, 510}},
                                      {{320, 510}, {320, 490}},
                                      {{500, 510}, {480, 510}},
                                      {{500, 510}, {500, 490}}},
                                     {34, 211, 238}, 2);
        fixture.Video().OSDDrawFilledRect({320, 218, 168, 51}, {16, 22, 36}, 232);
        fixture.Video().OSDDrawFilledRect({320, 221, 2, 45}, {34, 211, 238}, 255);
        fixture.Video().OSDDrawTextEx(327, 221, "smoke 39.7%", {235, 240, 249}, 13, {0, 0, 0}, 0, true, 0);
        fixture.Video().OSDDrawTextEx(327, 244, "176x240 px", {160, 174, 195}, 12, {0, 0, 0}, 0, true, 0);
        // Clipping and opacity on the source-frame boundary.
        fixture.Video().OSDDrawFilledRect({-5, -5, 20, 20}, {16, 22, 36}, 232);
        fixture.Video().EndOSD();
        const auto pixels = fixture.Download(frame);
        CHECK(pixels[static_cast<size_t>(255) * kWidth + 480] !=
              before[static_cast<size_t>(255) * kWidth + 480]);
        CHECK(pixels[0] != before[0]);
        CHECK(pixels[static_cast<size_t>(270) * kWidth + 400] ==
              before[static_cast<size_t>(270) * kWidth + 400]);
        CHECK(pixels[static_cast<size_t>(270) * kWidth + 325] !=
              before[static_cast<size_t>(270) * kWidth + 325]);
        SaveJpeg(fixture, frame, directory / "compact-corner-label.jpg");
    }
    SECTION("Missing font still renders queued lines and native fallback text") {
        SophonLineFixture fixture("");
        auto frame = fixture.Frame();
        REQUIRE(fixture.Video().BeginOSD(frame));
        fixture.Video().OSDDrawLines({{{20, 100}, {800, 100}}}, {168, 164, 255}, 2);
        fixture.Video().OSDDrawText(400, 200, "TEST", {255, 255, 255}, 20);
        fixture.Video().EndOSD();
        const auto pixels = fixture.Download(frame);
        CHECK(pixels[static_cast<size_t>(100) * kWidth + 200] != 128);
        bool textChanged = false;
        for (int y = 150; y < 240; ++y)
            for (int x = 350; x < 600; ++x)
                textChanged = textChanged || pixels[static_cast<size_t>(y) * kWidth + x] != 128;
        CHECK(textChanged);
        SaveJpeg(fixture, frame, directory / "missing-font.jpg");
    }
    SECTION("Concurrent sessions do not mix queued line groups") {
        using namespace std::chrono_literals;
        SophonLineFixture fixture(font);
        auto first  = fixture.Frame();
        auto second = fixture.Frame();
        REQUIRE(fixture.Video().BeginOSD(first));
        fixture.Video().OSDDrawLines({{{20, 100}, {800, 100}}}, {168, 164, 255}, 2);
        auto competing = std::async(std::launch::async, [&] {
            if (!fixture.Video().BeginOSD(second))
                return false;
            fixture.Video().OSDDrawLines({{{20, 250}, {800, 250}}}, {255, 107, 107}, 2);
            fixture.Video().EndOSD();
            return true;
        });
        CHECK(competing.wait_for(100ms) == std::future_status::timeout);
        fixture.Video().EndOSD();
        REQUIRE(competing.wait_for(10s) == std::future_status::ready);
        REQUIRE(competing.get());
        const auto a = fixture.Download(first);
        const auto b = fixture.Download(second);
        CHECK(a[static_cast<size_t>(100) * kWidth + 200] != 128);
        CHECK(a[static_cast<size_t>(250) * kWidth + 200] == 128);
        CHECK(b[static_cast<size_t>(100) * kWidth + 200] == 128);
        CHECK(b[static_cast<size_t>(250) * kWidth + 200] != 128);
    }
    SECTION("Renderer exceptions fall back safely and release the session") {
        ThrowingRenderer throwingRenderer;
        SophonLineFixture fixture("", kWidth, kHeight, 0, &throwingRenderer);
        auto first  = fixture.Frame();
        auto second = fixture.Frame();
        REQUIRE(fixture.Video().BeginOSD(first));
        fixture.Video().OSDDrawLines({{{20, 100}, {800, 100}}}, {168, 164, 255}, 2);
        fixture.Video().OSDDrawTextEx(400, 200, "TEST", {255, 255, 255}, 20, {0, 0, 0}, 180, true, 4);
        CHECK_NOTHROW(fixture.Video().EndOSD());
        const auto firstPixels = fixture.Download(first);
        CHECK(firstPixels[static_cast<size_t>(100) * kWidth + 200] != 128);

        // The next session on the same service must not see the failed queue.
        REQUIRE(fixture.Video().BeginOSD(second));
        fixture.Video().OSDDrawLines({{{20, 250}, {800, 250}}}, {255, 107, 107}, 2);
        CHECK_NOTHROW(fixture.Video().EndOSD());
        const auto secondPixels = fixture.Download(second);
        CHECK(secondPixels[static_cast<size_t>(100) * kWidth + 200] == 128);
        CHECK(secondPixels[static_cast<size_t>(250) * kWidth + 200] != 128);
    }
    SECTION("Unaligned even I420 dimensions render lines and text") {
        constexpr int width = 854, height = 480;
        SophonLineFixture fixture(font, width, height);
        auto frame = fixture.Frame();
        REQUIRE(frame->GetSize() == static_cast<size_t>(width * height * 3 / 2));
        REQUIRE(fixture.Video().BeginOSD(frame));
        fixture.Video().OSDDrawLines({{{30, 80}, {800, 120}}, {{100, 150}, {400, 350}}}, {168, 164, 255}, 2);
        fixture.Video().OSDDrawTextEx(100, 200, "TEST 92.0%", {255, 255, 255}, 20, {0, 0, 0}, 180, true, 4);
        fixture.Video().EndOSD();
        const auto pixels = fixture.Download(frame);
        CHECK(pixels[static_cast<size_t>(80) * width + 30] != 128);
        CHECK(pixels[static_cast<size_t>(150) * width + 100] != 128);
        bool textChanged = false;
        // The diagonal crosses farther right; this checks text on its own.
        for (int y = 190; y < 250; ++y)
            for (int x = 90; x < 150; ++x)
                textChanged = textChanged || pixels[static_cast<size_t>(y) * width + x] != 128;
        CHECK(textChanged);
        SaveJpeg(fixture, frame, directory / "unaligned-854x480.jpg");

        // Report session time, including both device-host transfers, in this
        // bounded single-stream synthetic test; this is not a capacity claim.
        std::vector<double> samples;
        for (int i = 0; i < 35; ++i) {
            const auto start = std::chrono::steady_clock::now();
            REQUIRE(fixture.Video().BeginOSD(frame));
            fixture.Video().OSDDrawLines({{{30, 80}, {800, 120}}, {{100, 150}, {400, 350}}}, {168, 164, 255},
                                         2);
            fixture.Video().OSDDrawTextEx(100, 200, "TEST 92.0%", {255, 255, 255}, 20, {0, 0, 0}, 180, true,
                                          4);
            fixture.Video().EndOSD();
            const auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (i >= 5)
                samples.push_back(ms);
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "Sophon 854x480 OSD session (2 lines + 1 label, 30 measured samples): p50="
                  << samples[15] << "ms p95=" << samples[28] << "ms p99=" << samples[29] << "ms\n";
    }
    SECTION("1080p area and multiple labelled targets, bounded single-stream measurement") {
        constexpr int width = 1920, height = 1080;
        // Two RGB-sized blocks suffice for one I420 source plus the temporary
        // RGB frame used by JPEG encoding (~12.4 MB total device allocation).
        // Pool refill warnings when the deliberate test cap is reached do not
        // indicate device memory exhaustion.
        SophonLineFixture fixture(font, width, height, 2);
        auto frame                   = fixture.Frame(true);
        auto original                = fixture.Download(frame);
        using Point                  = cosmo::util::Point;
        using Line                   = std::pair<Point, Point>;
        const std::vector<Line> area = {
            {{120, 95}, {1580, 150}},
            {{1580, 150}, {1650, 930}},
            {{1650, 930}, {300, 950}},
            {{300, 950}, {120, 95}},
        };
        std::vector<Line> detections;
        std::vector<Line> alarms;
        constexpr int boxCount = 5;
        for (int i = 0; i < boxCount; ++i) {
            const int x     = 250 + i * 280;
            constexpr int y = 380, w = 160, h = 260;
            auto& destination = i < 3 ? detections : alarms;
            destination.push_back({{x, y}, {x + w, y}});
            destination.push_back({{x + w, y}, {x + w, y + h}});
            destination.push_back({{x + w, y + h}, {x, y + h}});
            destination.push_back({{x, y + h}, {x, y}});
        }
        auto drawSession = [&] {
            REQUIRE(fixture.Video().BeginOSD(frame));
            fixture.Video().OSDDrawLines(area, {168, 164, 255}, 2);
            fixture.Video().OSDDrawLines(detections, {34, 211, 238}, 3);
            fixture.Video().OSDDrawLines(alarms, {255, 107, 107}, 2);
            for (int i = 0; i < boxCount; ++i) {
                const int x = 250 + i * 280;
                fixture.Video().OSDDrawTextEx(x, 310, "人员 92.0%", {235, 240, 249}, 19, {5, 8, 22}, 199,
                                              true, 4);
                fixture.Video().OSDDrawTextEx(x, 345, "160x260 px", {220, 231, 255}, 19, {5, 8, 22}, 199,
                                              true, 4);
            }
            fixture.Video().EndOSD();
        };
        std::vector<double> samples;
        for (int i = 0; i < 35; ++i) {
            // Reset the source outside the timed region to avoid repeatedly
            // compositing antialiased lines onto a previously marked frame.
            fixture.Upload(frame, original);
            const auto started = std::chrono::steady_clock::now();
            drawSession();
            const auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (i >= 5)
                samples.push_back(ms);
        }
        const auto result = fixture.Download(frame);
        CHECK(result[static_cast<size_t>(95) * width + 120] !=
              original[static_cast<size_t>(95) * width + 120]);
        for (int i = 0; i < boxCount; ++i) {
            const auto offset = static_cast<size_t>(380) * width + 300 + i * 280;
            CHECK(result[offset] != original[offset]);
        }
        bool labelChanged = false;
        for (int row = 302; row < 340; ++row)
            for (int x = 245; x < 450; ++x) {
                const auto offset = static_cast<size_t>(row) * width + x;
                labelChanged      = labelChanged || result[offset] != original[offset];
            }
        CHECK(labelChanged);
        SaveJpeg(fixture, frame, directory / "synthetic-1080p-area-and-targets.jpg");
        std::sort(samples.begin(), samples.end());
        REQUIRE(samples.size() == 30);
        std::cout << "Sophon synthetic 1920x1080 OSD session (1 area, 5 boxes, 10 text lines; "
                     "single stream, 30 measured samples): p50="
                  << samples[15] << "ms p95=" << samples[28] << "ms p99=" << samples[29] << "ms\n";
    }
    SECTION("Sophon rounded outlines use the shared CPU ring and preserve their interior") {
        SophonLineFixture fixture(font, 64, 48, 2);
        auto frame    = fixture.Frame();
        auto original = fixture.Download(frame);
        REQUIRE(fixture.Video().BeginOSD(frame));
        fixture.Video().OSDDrawRoundedRectOutline({4, 4, 24, 20}, {0, 0, 255}, 153, 5);
        fixture.Video().OSDDrawRoundedRectOutline({40, 4, 1, 1}, {0, 0, 255}, 255, 0);
        fixture.Video().EndOSD();
        const auto result = fixture.Download(frame);
        CHECK(result[4 * 64 + 4] == original[4 * 64 + 4]);
        CHECK(result[4 * 64 + 14] == 69);
        CHECK(result[12 * 64 + 14] == original[12 * 64 + 14]);
        CHECK(result[64 * 48 + 2 * 32 + 20] == 160);  // One luma pixel averages into its UV block.
        CHECK(result[64 * 48 + 6 * 32 + 7] == 128);
        fixture.Video().OSDDrawRoundedRectOutline({4, 4, 24, 20}, {0, 0, 0}, 255, 5);
        CHECK(fixture.Download(frame) == result);  // Session ownership stays enforced.
    }
    SECTION("1080p fine target style uses translucent panels and actual-pixel regular text") {
        constexpr int width = 1920, height = 1080, boxCount = 5;
        SophonLineFixture fixture(font, width, height, 2);
        // This fixture exercises the media primitives only. The strings and
        // rectangles are synthetic and do not validate inference or flow layout.
        auto frame    = fixture.Frame(true);
        auto original = fixture.Download(frame);
        cosmo::media::OsdTextRenderer metrics;
        REQUIRE(metrics.Init(font));
        using Point                  = cosmo::util::Point;
        using Line                   = std::pair<Point, Point>;
        const std::vector<Line> area = {
            {{120, 95}, {1580, 150}},
            {{1580, 150}, {1650, 930}},
            {{1650, 930}, {300, 950}},
            {{300, 950}, {120, 95}},
        };
        const auto textWidth = [&](const std::string& text, int pixels) {
            return metrics.RenderStringWithFineOutline(text, static_cast<float>(pixels)).width;
        };
        // Match the production refined card: 5 raw-label rows, standalone ID,
        // per-digit numeric runs and a separate 12px percent sign. Keep the
        // older 10-text-line benchmark above as a distinct, smaller workload.
        constexpr int panelWidth = 166, panelHeight = 113, panelY = 260, targetY = 380;
        const std::vector<std::string> labels = {"face", "happy", "down", "sad", "surprise"};
        const std::vector<std::string> scores = {"68.3", "0.8", "92.2", "4.1", "2.9"};
        int digitWidths[16]{};
        for (const int size : {14, 15}) {
            for (char digit = '0'; digit <= '9'; ++digit)
                digitWidths[size] = std::max(digitWidths[size], textWidth(std::string(1, digit), size));
            digitWidths[size] = std::max(1, digitWidths[size] - 2);
        }
        int textRuns        = 0;
        const auto drawText = [&](int x, int y, const std::string& value, cosmo::media::Color color,
                                  int size) {
            ++textRuns;
            fixture.Video().OSDDrawTextFine(x, y, value, color, size);
        };
        const auto drawScore = [&](int right, int y, const std::string& value, int size,
                                   cosmo::media::Color color) {
            const int percentWidth = textWidth("%", 12);
            const int decimalWidth = std::max(3, textWidth(".", size) - 2);
            int numberWidth        = 2;
            for (char ch : value)
                numberWidth += ch == '.' ? decimalWidth : digitWidths[size];
            int cursor = right - percentWidth - 3 - numberWidth;
            for (char ch : value) {
                const std::string glyph(1, ch);
                const int advance = ch == '.' ? decimalWidth : digitWidths[size];
                drawText(cursor + (advance - textWidth(glyph, size)) / 2, y, glyph, color, size);
                cursor += advance;
            }
            drawText(right - percentWidth, y + size - 12, "%", {212, 215, 231}, 12);
        };
        const int widths[boxCount]  = {49, 96, 160, 72, 120};
        const int heights[boxCount] = {71, 128, 260, 96, 180};
        auto drawSession            = [&] {
            textRuns = 0;
            REQUIRE(fixture.Video().BeginOSD(frame));
            fixture.Video().OSDDrawLines(area, {168, 164, 255}, 2);
            for (int i = 0; i < boxCount; ++i) {
                const int x = 250 + i * 280, w = widths[i], h = heights[i];
                const int arm = std::clamp(std::min(w, h) / 6, 6, 18);
                std::vector<Line> corners;
                const auto corner = [&](int px, int py, int dx, int dy) {
                    corners.push_back({{px + dx * 3, py}, {px + dx * arm, py}});
                    corners.push_back({{px + dx * 3, py}, {px + dx, py + dy}});
                    corners.push_back({{px + dx, py + dy}, {px, py + dy * 3}});
                    corners.push_back({{px, py + dy * 3}, {px, py + dy * arm}});
                };
                corner(x, targetY, 1, 1);
                corner(x + w, targetY, -1, 1);
                corner(x + w, targetY + h, -1, -1);
                corner(x, targetY + h, 1, -1);
                fixture.Video().OSDDrawLinesFine(corners, {91, 205, 214}, 2);
                fixture.Video().OSDDrawRoundedRect({x, panelY, panelWidth, panelHeight}, {52, 50, 71}, 145,
                                                              5);
                fixture.Video().OSDDrawRoundedRectOutline({x, panelY, panelWidth, panelHeight},
                                                                     {180, 177, 210}, 59, 5);
                int rowY = panelY + 8;
                for (size_t row = 0; row < labels.size(); ++row) {
                    const auto labelColor =
                        row == 0 ? cosmo::media::Color{245, 246, 251} : cosmo::media::Color{228, 230, 240};
                    drawText(x + 8, rowY, labels[row], labelColor, row == 0 ? 17 : 14);
                    if (row == 0) {
                        const auto id = "#" + std::to_string(17 + i);
                        const int idX = x + 8 + textWidth(labels[row], 17) + 10;
                        fixture.Video().OSDDrawRoundedRectOutline({idX, rowY - 1, textWidth(id, 13) + 10, 19},
                                                                             {179, 173, 208}, 64, 4);
                        drawText(idX + 5, rowY + 2, id, {220, 212, 238}, 13);
                        fixture.Video().OSDDrawRoundedRect({x + 8, rowY + 24, panelWidth - 16, 1},
                                                                      {184, 181, 212}, 33, 0);
                    }
                    drawScore(
                        x + panelWidth - 8, rowY + (row == 0 ? 1 : 0), scores[row], row == 0 ? 15 : 14,
                        row == 0 ? cosmo::media::Color{240, 242, 250} : cosmo::media::Color{230, 233, 243});
                    rowY += row == 0 ? 29 : 18;
                }
                drawText(x, targetY + h + 6, std::to_string(w) + "x" + std::to_string(h) + " px",
                                    {226, 230, 239}, 13);
            }
            fixture.Video().EndOSD();
            CHECK(textRuns == 145);  // 29 per target: 5 labels, ID, dimensions, and 22 score glyph runs.
        };
        std::vector<double> samples;
        for (int i = 0; i < 35; ++i) {
            fixture.Upload(frame, original);
            const auto started = std::chrono::steady_clock::now();
            drawSession();
            const auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (i >= 5)
                samples.push_back(ms);
        }
        const auto result = fixture.Download(frame);
        const auto at     = [&](int x, int y) { return static_cast<size_t>(y) * width + x; };
        CHECK(result[at(120, 95)] != original[at(120, 95)]);
        for (int i = 0; i < boxCount; ++i) {
            const int x = 250 + i * 280;
            CHECK(result[at(x + 4, targetY)] != original[at(x + 4, targetY)]);
            CHECK(result[at(x + 4, targetY - 2)] == original[at(x + 4, targetY - 2)]);  // No dark halo.
            CHECK(result[at(x, panelY)] == original[at(x, panelY)]);  // Rounded corner remains clear.
            const auto gap = at(x + panelWidth - 3, panelY + 22);
            CHECK(result[gap] == (145 * 52 + 110 * original[gap] + 127) / 255);
            // Check each row and the independently positioned ID/percent fields.
            for (const auto& band : std::vector<std::pair<int, int>>{{panelY + 8, 17},
                                                                     {panelY + 37, 14},
                                                                     {panelY + 55, 14},
                                                                     {panelY + 73, 14},
                                                                     {panelY + 91, 14},
                                                                     {targetY + heights[i] + 6, 13}}) {
                bool textPresent = false;
                for (int row = band.first; row < band.first + band.second; ++row)
                    for (int col = x; col < x + panelWidth; ++col)
                        textPresent = textPresent || (result[at(col, row)] != original[at(col, row)] &&
                                                      result[at(col, row)] > 140);
                CHECK(textPresent);
            }
            const int idX = x + 8 + textWidth("face", 17) + 15;
            for (const auto& field : std::vector<cosmo::util::Box>{
                     {idX, panelY + 10, textWidth("#" + std::to_string(17 + i), 13), 13},
                     {x + panelWidth - 8 - textWidth("%", 12), panelY + 12, textWidth("%", 12), 12}}) {
                bool glyphPresent = false;
                for (int py = field.y; py < field.y + field.height; ++py)
                    for (int px = field.x; px < field.x + field.width; ++px)
                        glyphPresent = glyphPresent || result[at(px, py)] > 150;
                CHECK(glyphPresent);
            }
        }
        SaveJpeg(fixture, frame, directory / "fine-target-style.jpg");
        const auto jpeg = fixture.Video().EncodeJpeg(frame, 95);
        cosmo::media::EncodedImageInfo imageInfo;
        REQUIRE(cosmo::media::InspectEncodedImage(jpeg, imageInfo));
        CHECK(imageInfo.width == width);
        CHECK(imageInfo.height == height);
        std::sort(samples.begin(), samples.end());
        REQUIRE(samples.size() == 30);
        std::cout << "Sophon synthetic 1920x1080 fine OSD session (1 area, 5 corner boxes, 5 panels, "
                     "5 ID outline markers, 5 header rules, 145 text runs at 17/15/14/13/12px; "
                     "media-only single stream, 30 measured samples): p50="
                  << samples[15] << "ms p95=" << samples[28] << "ms p99=" << samples[29] << "ms\n";
    }
    INFO("Private artifacts: " << directory.string());
}
#endif
