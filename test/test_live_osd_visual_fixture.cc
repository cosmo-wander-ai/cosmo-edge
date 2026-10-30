// Opt-in layout fixture: all target labels, IDs, confidences and geometry are synthetic.
// Input is a raw 1920x1080 RGB24 frame; output is lossless RGB PPM.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/IOsdTextRenderer.h"
#include "media/OsdTextRenderer.h"
#include "media/PixelFormat.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorCpu.h"
#include "mem/DeviceContext.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "mock/MockTaskService.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/dto/TaskAreaTypes.h"
#define private public
#include "flow/stream/StreamViewerOverview.h"
#undef private
#include "flow/stream/StreamViewerOverviewTypes.h"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
namespace {
using namespace cosmo;
constexpr int kWidth    = 1920;
constexpr int kHeight   = 1080;
constexpr size_t kBytes = static_cast<size_t>(kWidth) * kHeight * 3;

MsgTarget fixtureTarget(int x, int y, int width, int height, bool manyLabels, int trackId = -1) {
    MsgTarget target{};
    target.trackId      = trackId;
    target.aiBox.x      = x;
    target.aiBox.y      = y;
    target.aiBox.width  = width;
    target.aiBox.height = height;
    target.confidence.push_back({"face", 0.683F, true});
    if (manyLabels) {
        target.confidence.push_back({"happy", 0.066F, true});
    }
    target.confidence.push_back({"down", 0.922F, true});
    if (manyLabels) {
        target.confidence.push_back({"sad", 0.051F, true});
        target.confidence.push_back({"surprise", 0.031F, true});
    }
    return target;
}

MsgTarget refinedFixtureTarget(int x, int y, int width, int height, int trackId) {
    auto target = fixtureTarget(x, y, width, height, false, trackId);
    // These demonstration values match the approved still, not any live inference.
    target.confidence = {{"face", 0.683F, true},
                         {"happy", 0.008F, true},
                         {"down", 0.922F, true},
                         {"sad", 0.041F, true},
                         {"surprise", 0.029F, true}};
    return target;
}

struct ResetMemoryPool {
    ~ResetMemoryPool() {
        mem::SetMemoryPoolContext(nullptr);
    }
};

TEST_CASE("Render real CPU live OSD on a supplied frame for visual review",
          "[stream][osd][.visual-fixture]") {
    const char* input  = std::getenv("COSMO_LIVE_OSD_FIXTURE_INPUT");
    const char* output = std::getenv("COSMO_LIVE_OSD_FIXTURE_OUTPUT");
    const char* font   = std::getenv("COSMO_ALARM_ANNOTATION_FONT");
    if (!input || !*input || !output || !*output || !font || !*font) {
        SKIP(
            "Set COSMO_LIVE_OSD_FIXTURE_INPUT, COSMO_LIVE_OSD_FIXTURE_OUTPUT and "
            "COSMO_ALARM_ANNOTATION_FONT");
    }
    std::ifstream source(input, std::ios::binary | std::ios::ate);
    REQUIRE(source.is_open());
    REQUIRE(source.tellg() == static_cast<std::streamoff>(kBytes));
    source.seekg(0);
    std::vector<uint8_t> pixels(kBytes);
    REQUIRE(static_cast<bool>(
        source.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()))));
    REQUIRE(std::filesystem::is_directory(output));

    mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {static_cast<int>(kBytes)});
    mem::SetMemoryPoolContext(&pool);
    ResetMemoryPool reset;
    mem::DeviceContext device;
    test::ScopedServiceOverride<mem::IDeviceContext> deviceRegistration(device);
    media::OsdTextRenderer renderer;
    REQUIRE(renderer.Init(font));
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    service::VideoFrameServiceImpl osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> taskRegistration(tasks);

    const auto render = [&](const std::string& filename, std::vector<MsgTarget> targets) {
        auto frame =
            std::make_shared<media::VideoFrame>(kWidth, kHeight, media::PixelFormat::PIXEL_RGB8, 42, 10000);
        REQUIRE(frame->Active());
        REQUIRE(frame->GetWidth() == kWidth);
        REQUIRE(frame->GetHeight() == kHeight);
        REQUIRE(frame->GetSize() >= kBytes);
        REQUIRE(frame->GetData() != nullptr);
        std::memcpy(frame->GetData(), pixels.data(), kBytes);
        frame->SetStreamIndex(1);
        StreamViewerOverview viewer("fixture-camera", "fixture-algorithm");
        viewer.width_                      = kWidth;
        viewer.height_                     = kHeight;
        viewer.frame_identity_.streamIndex = 1;
        viewer.frame_identity_.index       = 42;
        viewer.frame_identity_.timestamp   = 10000;
        for (auto& target : targets) {
            viewer.LiveDataHandTarget(1, 42, 10000, target);
        }
        // Verify that the genuine live-data path carries each supplied track ID
        // once, instead of adding an ID only inside a mock image renderer.
        for (const auto& target : targets) {
            if (target.trackId < 0)
                continue;
            size_t matchingIds = 0;
            for (const auto& overview : viewer.infos_.overviews) {
                for (const auto& group : overview.texts) {
                    for (const auto& row : group.posTexts) {
                        if (row.targetLabel && row.targetLabel->trackId == target.trackId)
                            ++matchingIds;
                    }
                }
            }
            REQUIRE(matchingIds == 1);
        }
        auto info = viewer.GetOverviewDataFromLocal(frame);
        REQUIRE(osd.BeginOSD(frame));
        viewer.FrameOverview(frame, info);
        osd.EndOSD();
        CHECK(frame->GetWidth() == kWidth);
        CHECK(frame->GetHeight() == kHeight);
        // Check the vicinity of the first target, rather than merely relying on file existence.
        const auto& box  = targets.front().aiBox;
        const int left   = std::max(0, box.x - 5);
        const int right  = std::min(kWidth, box.x + box.width + 6);
        const int top    = std::max(0, box.y - 5);
        const int bottom = std::min(kHeight, box.y + box.height + 6);
        size_t changed   = 0;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const size_t offset = (static_cast<size_t>(y) * kWidth + x) * 3;
                if (std::memcmp(frame->GetData() + offset, pixels.data() + offset, 3) != 0)
                    ++changed;
            }
        }
        CHECK(changed > 0);
        const auto path = std::filesystem::path(output) / filename;
        std::ofstream image(path, std::ios::binary | std::ios::trunc);
        REQUIRE(image.is_open());
        image << "P6\n" << kWidth << " " << kHeight << "\n255\n";
        image.write(reinterpret_cast<const char*>(frame->GetData()), static_cast<std::streamsize>(kBytes));
        image.close();
        REQUIRE(image.good());
        CHECK(std::filesystem::file_size(path) > kBytes);
    };
    render("two-label.ppm", {fixtureTarget(984, 406, 49, 71, false)});
    render("six-label.ppm", {fixtureTarget(984, 406, 49, 71, true)});
    render("edge-target.ppm", {fixtureTarget(1857, 8, 49, 71, true)});
    render("multiple-targets.ppm",
           {fixtureTarget(984, 406, 49, 71, true), fixtureTarget(1070, 420, 56, 82, false),
            fixtureTarget(1110, 385, 49, 71, true)});
    render("fine-id-preview.ppm", {refinedFixtureTarget(984, 406, 49, 71, 17)});
    auto longIdTarget = refinedFixtureTarget(1857, 8, 49, 71, std::numeric_limits<int>::max());
    longIdTarget.confidence.front().label = "person_with_a_very_long_raw_label";
    render("fine-id-edge-long.ppm", {longIdTarget});
    render("fine-id-multiple.ppm",
           {refinedFixtureTarget(984, 406, 49, 71, 17), refinedFixtureTarget(1070, 420, 56, 82, 18),
            refinedFixtureTarget(1110, 385, 49, 71, 19)});
}
}  // namespace
#endif
