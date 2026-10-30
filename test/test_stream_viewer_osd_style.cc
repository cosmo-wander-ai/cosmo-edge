#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <deque>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/common/AlgDataRecord.h"
#include "flow/common/AlgDataUnit.h"
#include "media/Color.h"
#include "media/IOsdTextRenderer.h"
#include "media/PixelFormat.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "mock/MockTaskService.h"
#include "nlohmann/json.hpp"
#include "service/media/IVideoFrameOSD.h"
#include "support/ScopedServiceOverride.h"
#include "util/AiTypes.h"
#include "util/dto/TaskAreaTypes.h"
#define private public
#include "flow/overview/OverviewRecordAiRst.h"
#include "flow/stream/StreamViewerOverview.h"
#undef private
#include "flow/stream/StreamViewerOverviewTypes.h"

namespace {
using namespace cosmo;

class CapturingOSD final : public service::IVideoFrameOSD {
public:
    struct Lines {
        std::vector<std::pair<util::Point, util::Point>> segments;
        media::Color color;
        int width;
    };
    std::vector<Lines> lines;
    std::vector<std::string> texts;
    std::vector<std::string> glyphs;
    std::vector<int> glyphX, glyphY, glyphFont;
    std::vector<media::Color> glyphColor;
    std::vector<util::Box> outlines;
    std::string pendingNumber;
    int pendingX{0}, pendingY{0}, pendingFont{0}, previousGlyphX{0};
    void FlushNumber() {
        if (pendingNumber.empty())
            return;
        texts.push_back(pendingNumber);
        textX.push_back(pendingX);
        textY.push_back(pendingY);
        textFont.push_back(pendingFont);
        pendingNumber.clear();
    }
    std::vector<int> textX;
    std::vector<int> textY;
    std::vector<int> textFont;
    std::vector<util::Box> filledRects;
    std::vector<util::Box> roundedRects;
    std::vector<uint8_t> roundedOpacity;
    int fineLineCalls{0};
    int begins{0};
    int ends{0};
    bool throwBegin{false};
    bool throwDraw{false};
    bool throwEnd{false};
    bool throwNonStandardEnd{false};
    bool active{false};
    std::mutex sessionMutex;
    VideoFramePtr CopyJpegSrcFrame(VideoFramePtr src) override {
        return src;
    }
    VideoFramePtr DrawLines(VideoFramePtr src, std::vector<std::pair<util::Point, util::Point>>,
                            const media::Color&, int) override {
        return src;
    }
    VideoFramePtr DrawRects(VideoFramePtr src, const std::vector<util::Box>&, const media::Color&,
                            int) override {
        return src;
    }
    VideoFramePtr DrawText(VideoFramePtr src, int, int, const std::string&, const media::Color&,
                           int) override {
        return src;
    }
    bool BeginOSD(VideoFramePtr) override {
        ++begins;
        if (throwBegin)
            throw std::bad_alloc();
        if (!sessionMutex.try_lock())
            return false;
        active = true;
        return true;
    }
    void OSDDrawLines(std::vector<std::pair<util::Point, util::Point>> segments, const media::Color& color,
                      int width) override {
        if (throwDraw)
            throw std::bad_alloc();
        lines.push_back({std::move(segments), color, width});
    }
    void OSDDrawLinesFine(std::vector<std::pair<util::Point, util::Point>> segments,
                          const media::Color& color, int width) override {
        ++fineLineCalls;
        OSDDrawLines(std::move(segments), color, width);
    }
    void OSDDrawFilledRect(const util::Box& rect, const media::Color&, uint8_t) override {
        filledRects.push_back(rect);
    }
    void OSDDrawRoundedRect(const util::Box& rect, const media::Color&, uint8_t opacity, int) override {
        if (rect.height == 1) {
            filledRects.push_back(rect);
            return;
        }
        roundedRects.push_back(rect);
        roundedOpacity.push_back(opacity);
    }
    void OSDDrawRoundedRectOutline(const util::Box& rect, const media::Color&, uint8_t, int) override {
        outlines.push_back(rect);
    }
    void OSDDrawTextFine(int x, int y, const std::string& text, const media::Color& color,
                         int pixelHeight) override {
        glyphs.push_back(text);
        glyphX.push_back(x);
        glyphY.push_back(y);
        glyphFont.push_back(pixelHeight);
        glyphColor.push_back(color);
        // Reconstruct numeric paint runs for semantic assertions, while keeping
        // every original glyph call above for geometry and typography checks.
        if (text.size() == 1 && (std::isdigit(static_cast<unsigned char>(text[0])) || text == ".")) {
            if (!pendingNumber.empty() &&
                (pendingFont != pixelHeight || pendingY != y || x > previousGlyphX + pixelHeight + 2))
                FlushNumber();
            if (pendingNumber.empty()) {
                pendingX    = x;
                pendingY    = y;
                pendingFont = pixelHeight;
            }
            pendingNumber += text;
            previousGlyphX = x;
            return;
        }
        if (text == "%" && !pendingNumber.empty()) {
            pendingNumber += "%";
            FlushNumber();
            return;
        }
        FlushNumber();
        texts.push_back(text);
        textX.push_back(x);
        textY.push_back(y);
        textFont.push_back(pixelHeight);
    }
    void OSDDrawText(int x, int, const std::string& text, const media::Color&, int) override {
        texts.push_back(text);
        textX.push_back(x);
    }
    void OSDDrawTextEx(int x, int y, const std::string& text, const media::Color&, int font,
                       const media::Color&, uint8_t, bool, int) override {
        texts.push_back(text);
        textX.push_back(x);
        textY.push_back(y);
        textFont.push_back(font);
    }
    void EndOSD() override {
        ++ends;
        if (active) {
            active = false;
            sessionMutex.unlock();
        }
        if (throwEnd)
            throw std::bad_alloc();
        if (throwNonStandardEnd)
            throw 1;
    }
};

class WidthRenderer final : public media::IOsdTextRenderer {
public:
    mutable int calls{0};
    mutable float pixelHeight{0};
    bool ready{true};
    bool Init(const std::string&) override {
        return true;
    }
    bool IsReady() const override {
        return ready;
    }
    TextBitmap RenderString(const std::string&, float) const override {
        return {};
    }
    OutlinedTextBitmap RenderStringWithOutline(const std::string& text, float height) const override {
        ++calls;
        pixelHeight    = height;
        int codepoints = 0;
        for (unsigned char c : text)
            if ((c & 0xc0) != 0x80)
                ++codepoints;
        OutlinedTextBitmap result;
        result.width = static_cast<int>(std::ceil(codepoints * height));
        return result;
    }
    OutlinedTextBitmap RenderStringWithFineOutline(const std::string& text, float height) const override {
        return RenderStringWithOutline(text, height);
    }
};

TEST_CASE("Live labels fit measured UTF-8 text and retain score near the image edge",
          "[stream][osd][text-layout]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> taskRegistration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    // Exercise actual overflow even after the intentionally smaller OSD font.
    viewer.width_  = 300;
    viewer.height_ = 360;
    StreamOverviewTextEl label;
    label.attrPriority = VideoOverviewAttrPriority::kConfidence;
    label.hasBgColor   = true;
    label.text = "很长的中文自定义检测类别名称用于测试不会被边缘裁切 92.0% #7";
    StreamViewerOverview::OverviewInfo data;
    StreamOverviewText block;
    block.pos = {295, 250};
    block.posTexts.push_back(label);
    data.texts.push_back(block);

    viewer.FrameOverview(nullptr, data);
    REQUIRE(osd.texts.size() == 1);
    CHECK(osd.texts[0].find("...") != std::string::npos);
    CHECK(osd.texts[0].find(" 92.0% #7") != std::string::npos);
    CHECK(osd.textX[0] >= 0);
    auto fitted = label;
    fitted.text = osd.texts[0];
    CHECK(osd.textX[0] + viewer.GetTextWidth(fitted) < viewer.width_);
#if defined(COSMO_MEDIA_USE_SOPHON_BACKEND)
    CHECK(renderer.pixelHeight == Catch::Approx(19.5F));
#else
    CHECK(renderer.pixelHeight == Catch::Approx(13.0F));
#endif
    const auto firstCalls = renderer.calls;
    viewer.FrameOverview(nullptr, data);
    CHECK(renderer.calls == firstCalls);
    renderer.ready = false;
    auto fallback  = label;
    fallback.text  = "test";
    CHECK(viewer.GetTextWidth(fallback) == 44);  // prior 11px per UTF-8 character
}

TEST_CASE("Live OSD sends every target segment when a color exceeds the backend batch limit",
          "[stream][osd][batching]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> taskRegistration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    StreamViewerOverview viewer("camera", "algorithm");
    StreamViewerOverview::OverviewInfo data;
    auto& group     = data.attrLines[1];
    group.color     = {34, 211, 238};
    group.lineWidth = 3;
    group.fine      = GENERATE(false, true);
    for (int i = 0; i < 260; ++i)
        group.lines.push_back({{i, 1}, {i, 2}});
    viewer.FrameOverview(nullptr, data);
    REQUIRE(osd.lines.size() == 3);
    CHECK(osd.fineLineCalls == (group.fine ? 3 : 0));
    CHECK(osd.lines[0].segments.size() == 128);
    CHECK(osd.lines[1].segments.size() == 128);
    CHECK(osd.lines[2].segments.size() == 4);
    CHECK(osd.lines[0].segments.front().first.x == 0);
    CHECK(osd.lines[2].segments.back().first.x == 259);
}

TEST_CASE("Live target label pairs original labels and aligned scores within a bounded panel",
          "[stream][osd][target-panel]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> taskRegistration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_  = 640;
    viewer.height_ = 360;
    StreamViewerOverview::OverviewInfo data;
    StreamOverviewText block;
    block.pos       = {630, 2};
    block.targetBox = util::Box{630, 2, 120, 200};
    StreamOverviewTextEl label;
    label.attrPriority             = VideoOverviewAttrPriority::kConfidence;
    label.text                     = "person 92.0%";
    label.targetLabel              = StreamOverviewTextEl::TargetLabel{"person", 0.92F, -1};
    StreamOverviewTextEl attribute = label;
    attribute.text                 = "raw 7% #x 86.0%";
    attribute.targetLabel          = StreamOverviewTextEl::TargetLabel{"raw 7% #x", 0.86F, -1};
    StreamOverviewTextEl size;
    size.attrPriority = VideoOverviewAttrPriority::kBoxSize;
    size.text         = "120x200 px";
    block.posTexts    = {label, size, attribute};
    data.texts.push_back(block);
    viewer.FrameOverview(nullptr, data);
    CHECK(osd.filledRects.size() == 1);  // one subtle header separator, no accent bar
    REQUIRE(osd.roundedRects.size() == 1);
    CHECK(osd.roundedOpacity.front() == 145);
    const auto panel = osd.roundedRects.front();
    CHECK(panel.x >= 0);
    CHECK(panel.y >= 0);
    CHECK(panel.x + panel.width <= 640);
    CHECK(panel.y + panel.height <= 360);
    REQUIRE(osd.texts.size() == 5);
    CHECK(osd.texts[0] == "person");
    CHECK(osd.texts[1] == "92.0%");
    CHECK(osd.texts[2] == "raw 7% #x");
    CHECK(osd.texts[3] == "86.0%");
    CHECK(osd.texts[4] == "120x200 px");
    CHECK(osd.textX[0] == osd.textX[2]);
    CHECK(osd.textY[0] + 1 == osd.textY[1]);
    CHECK(osd.textY[2] == osd.textY[3]);
    CHECK(osd.textFont[1] == 15);
    CHECK(osd.textFont[3] == 14);
    std::vector<int> percentX;
    for (size_t i = 0; i < osd.glyphs.size(); ++i)
        if (osd.glyphs[i] == "%") {
            CHECK(osd.glyphFont[i] == 12);
            percentX.push_back(osd.glyphX[i]);
        }
    // Small secondary fields keep their hierarchy while remaining bright on the
    // unchanged translucent surface. Check actual paint calls, including digits.
    for (size_t i = 0; i < osd.glyphs.size(); ++i) {
        const auto expected = osd.glyphs[i] == "person"       ? media::Color{245, 246, 251}
                              : osd.glyphs[i] == "raw 7% #x"  ? media::Color{228, 230, 240}
                              : osd.glyphs[i] == "120x200 px" ? media::Color{226, 230, 239}
                              : osd.glyphs[i] == "%"          ? media::Color{212, 215, 231}
                              : osd.glyphY[i] == osd.textY[1] ? media::Color{240, 242, 250}
                                                              : media::Color{230, 233, 243};
        CHECK(osd.glyphColor[i].red == expected.red);
        CHECK(osd.glyphColor[i].green == expected.green);
        CHECK(osd.glyphColor[i].blue == expected.blue);
    }
    REQUIRE(percentX.size() == 2);
    CHECK(percentX[0] == percentX[1]);
    CHECK(osd.textFont[0] == 17);
    CHECK(osd.textFont[2] == 14);
    CHECK(osd.textFont[4] == 13);
    // The target extends beyond the right edge. The size caption may sit beside
    // its bottom edge after the panel occupies the default below-box position.
    const util::Box caption{osd.textX[4], osd.textY[4], viewer.GetFineTextWidth(osd.texts[4], 13), 15};
    const auto intersects = [](const util::Box& a, const util::Box& b) {
        return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
    };
    const auto& target = *block.targetBox;
    CHECK_FALSE(intersects(caption, panel));
    CHECK_FALSE(intersects(caption, target));
    const int horizontalGap =
        std::max({0, target.x - caption.x - caption.width, caption.x - target.x - target.width});
    const int verticalGap =
        std::max({0, target.y - caption.y - caption.height, caption.y - target.y - target.height});
    CHECK(horizontalGap + verticalGap <= 14);  // caption remains adjacent to its target
    for (size_t i = 0; i < osd.glyphs.size(); ++i) {
        CHECK(osd.glyphX[i] >= 0);
        CHECK(osd.glyphX[i] + viewer.GetFineTextWidth(osd.glyphs[i], osd.glyphFont[i]) <= 640);
        CHECK(osd.glyphY[i] + osd.glyphFont[i] + 2 <= 360);
    }
}

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
TEST_CASE("Live OSD keeps area geometry and timing adaptation without drawing diagnostics",
          "[stream][osd][timing]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    util::DurationStatInfo detect;
    detect.duration_ns = 200000000;
    detect.count       = 1;
    util::DurationStatInfo alarm;
    alarm.duration_ns                                                           = 800000000;
    alarm.count                                                                 = 1;
    const std::vector<std::pair<std::string, util::DurationStatInfo>> durations = {{"Detect", detect},
                                                                                   {"Alarm", alarm}};
    ALLOW_CALL(tasks, GetTaskActionDurations(trompeloeil::_, trompeloeil::_)).LR_RETURN(durations);
    test::ScopedServiceOverride<service::ITaskQuery> taskRegistration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);

    constexpr int width  = 640;
    constexpr int height = 360;
    mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {width * height * 3 / 2});
    mem::SetMemoryPoolContext(&pool);
    struct ResetPool {
        ~ResetPool() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } resetPool;
    auto frame = std::make_shared<media::VideoFrame>(width, height, media::PixelFormat::PIXEL_I420, 2, 5000);
    REQUIRE(VideoFrameValid(frame));
    frame->SetStreamIndex(1);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_                          = width;
    viewer.height_                         = height;
    viewer.hand_data_count_                = 1;  // Keep the directly prepared test data.
    viewer.fresh_frame_identity_.timestamp = 5000;
    StreamOverviewEl oldResult;
    oldResult.streamIndex = 1;
    oldResult.index       = 2;
    oldResult.timestamp   = 4500;
    viewer.infos_.overviews.push_back(oldResult);
    MsgTaskArea area;
    area.points = {{0.1, 0.1}, {0.8, 0.1}, {0.8, 0.8}, {0.1, 0.8}};
    viewer.params_.areas.push_back(area);
    viewer.osd_duration_.duration_ns = 9000000;
    viewer.osd_duration_.count       = 1;

    viewer.HandLiveOverview(frame);
    CHECK(osd.begins == 1);
    CHECK(osd.ends == 1);
    CHECK(osd.texts.empty());
    REQUIRE(osd.lines.size() == 1);
    const auto& polygon = osd.lines.front();
    REQUIRE(polygon.segments.size() == 4);
    CHECK(polygon.color.red == 168);
    CHECK(polygon.color.green == 164);
    CHECK(polygon.color.blue == 255);
    CHECK(polygon.segments[0].first.x == 64);
    CHECK(polygon.segments[0].first.y == 36);
    CHECK(polygon.segments[0].second.x == 512);
    CHECK(polygon.segments[0].second.y == 36);
    CHECK(viewer.cache_durations_.size() == 2);
    CHECK(viewer.frame_duration_ == 500);  // max(observed latency, 1.5 * detection duration)
    CHECK(viewer.cache_osd_duration_.count == 1);
    CHECK(viewer.osd_duration_.count == 0);
}

TEST_CASE("Live preview survives OSD exceptions and releases its session for later frames",
          "[stream][osd][exceptions]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    const std::vector<std::pair<std::string, util::DurationStatInfo>> durations;
    ALLOW_CALL(tasks, GetTaskActionDurations(trompeloeil::_, trompeloeil::_)).LR_RETURN(durations);
    test::ScopedServiceOverride<service::ITaskQuery> taskRegistration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);

    constexpr int width = 64, height = 64;
    mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {width * height * 3 / 2});
    mem::SetMemoryPoolContext(&pool);
    struct ResetPool {
        ~ResetPool() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } resetPool;
    auto frame = std::make_shared<media::VideoFrame>(width, height, media::PixelFormat::PIXEL_I420, 2, 5000);
    REQUIRE(VideoFrameValid(frame));
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_           = width;
    viewer.height_          = height;
    viewer.hand_data_count_ = 1;
    MsgTaskArea area;
    area.points = {{0.1, 0.1}, {0.8, 0.1}, {0.8, 0.8}};
    viewer.params_.areas.push_back(area);

    SECTION("Begin failure does not leave a session open") {
        osd.throwBegin = true;
        REQUIRE_NOTHROW(viewer.HandOverview(frame));
        CHECK(osd.ends == 0);
        CHECK_FALSE(osd.active);
        osd.throwBegin = false;
    }
    SECTION("Drawing and flush failures do not terminate or retain the session") {
        osd.throwDraw = true;
        osd.throwEnd  = true;
        REQUIRE_NOTHROW(viewer.HandOverview(frame));
        CHECK(osd.ends == 1);
        CHECK_FALSE(osd.active);
        osd.throwDraw = false;
        osd.throwEnd  = false;
    }
    SECTION("Flush failure alone does not terminate or retain the session") {
        osd.throwEnd = true;
        REQUIRE_NOTHROW(viewer.HandOverview(frame));
        CHECK(osd.ends == 1);
        CHECK_FALSE(osd.active);
        osd.throwEnd = false;
    }
    SECTION("An unknown flush exception is contained as well") {
        osd.throwNonStandardEnd = true;
        REQUIRE_NOTHROW(viewer.HandOverview(frame));
        CHECK(osd.ends == 1);
        CHECK_FALSE(osd.active);
        osd.throwNonStandardEnd = false;
    }

    // A subsequent frame must be able to start and finish a fresh session.
    const auto previousEnds = osd.ends;
    REQUIRE_NOTHROW(viewer.HandOverview(frame));
    CHECK(osd.ends == previousEnds + 1);
    CHECK_FALSE(osd.active);
}
#endif

TEST_CASE("Live OSD displays only measured, valid confidence scores", "[stream][osd][provenance]") {
    auto source       = std::make_shared<DataDetTrackClassify>();
    source->picWidth  = 640;
    source->picHeight = 360;
    AiDetectRstEl target;
    target.trackId     = 7;
    target.box         = {100, 90, 120, 200};
    target.confidence  = {"person", "detector", 0.92F, AiAnnotationSource::Detection};
    target.classifyRst = {
        {"helmet", "classifier", 0.86F, AiAnnotationSource::Classification},
        {"vlm verdict", "vlm", 1.0F, AiAnnotationSource::Unknown},
        {"placeholder", "classifier", 1.0F, AiAnnotationSource::Unknown},
        {"invalid", "detector", 1.25F, AiAnnotationSource::Detection},
    };
    source->targets.push_back(target);
    OverviewRecordAiRst recorder("task", "action");
    const auto overview = recorder.SrcData2MsgData(source);
    REQUIRE(overview.targets.size() == 1);
    auto liveTarget = overview.targets.front();
    REQUIRE(liveTarget.confidence.size() == 5);
    CHECK(liveTarget.confidence[0].scoreMeasured);
    CHECK(liveTarget.confidence[1].scoreMeasured);
    CHECK_FALSE(liveTarget.confidence[2].scoreMeasured);
    CHECK_FALSE(liveTarget.confidence[3].scoreMeasured);
    CHECK_FALSE(liveTarget.confidence[4].scoreMeasured);

    // Keep the existing externally visible JSON shape; decoded historical data
    // cannot claim measurement provenance, including when an object is reused.
    const nlohmann::json json = liveTarget.confidence[0];
    REQUIRE(json.size() == 2);
    CHECK(json.contains("label"));
    CHECK(json.contains("confidence"));
    auto decoded = liveTarget.confidence[0];
    REQUIRE(decoded.scoreMeasured);
    json.get_to(decoded);
    CHECK_FALSE(decoded.scoreMeasured);
    CHECK(decoded.confidence == Catch::Approx(0.92F));

    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_                      = 640;
    viewer.height_                     = 360;
    viewer.frame_identity_.streamIndex = 1;
    viewer.LiveDataHandTarget(1, 8, 1000, liveTarget);

    REQUIRE(viewer.infos_.overviews.size() == 1);
    const auto& drawn = viewer.infos_.overviews.front();
    REQUIRE(drawn.lines.size() == 16);
    CHECK(drawn.lines[0].line.first.x == 103);
    CHECK(drawn.lines[0].line.second.x == 118);  // shorter bracket and 2 px softened corner
    CHECK(drawn.lines[3].line.second.y == 108);
    for (const auto& line : drawn.lines) {
        CHECK(line.color.red == 91);
        CHECK(line.color.green == 205);
        CHECK(line.color.blue == 214);
    }
    REQUIRE(drawn.texts.size() == 1);
    const auto& labels = drawn.texts.front().posTexts;
    REQUIRE(labels.size() == 6);
    CHECK(labels[0].text == "person 92.0% #7");
    CHECK(labels[1].text == "120x200 px");
    CHECK(labels[2].text == "helmet 86.0%");
    CHECK(labels[3].text == "vlm verdict");
    CHECK(labels[4].text == "placeholder");
    CHECK(labels[5].text == "invalid");
    REQUIRE(labels[0].targetLabel);
    CHECK(labels[0].targetLabel->label == "person");
    REQUIRE(labels[0].targetLabel->score);
    CHECK(*labels[0].targetLabel->score == Catch::Approx(0.92F));
    CHECK(labels[0].targetLabel->trackId == 7);
    CHECK_FALSE(labels[3].targetLabel->score);
    CHECK_FALSE(labels[5].targetLabel->score);
}

TEST_CASE("Very small live targets keep a legible full outline", "[stream][osd][small-target]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_                      = 640;
    viewer.height_                     = 360;
    viewer.frame_identity_.streamIndex = 1;
    MsgTarget target{};
    target.trackId = -1;
    target.aiBox   = {0, 2, 12, 18};
    target.confidence.push_back({"person", 0.9F, true});
    viewer.LiveDataHandTarget(1, 8, 1000, target);
    REQUIRE(viewer.infos_.overviews.size() == 1);
    REQUIRE(viewer.infos_.overviews[0].lines.size() == 4);
    CHECK(viewer.infos_.overviews[0].lines[0].line.first.x == 0);
    CHECK(viewer.infos_.overviews[0].lines[0].line.second.x == 12);
}

TEST_CASE("Live target panels preserve all raw results and report physical overflow",
          "[stream][osd][target-panel]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_  = 640;
    viewer.height_ = 480;
    MsgTarget target{};
    target.aiBox      = {100, 300, 80, 100};
    target.confidence = {{"raw", 0.5F, true}, {"raw extended", 0.4F, true}};
    for (int i = 0; i < 10; ++i)
        target.confidence.push_back({"item" + std::to_string(i), 0.3F, true});
    viewer.LiveDataHandTarget(0, 1, 1000, target);
    REQUIRE(viewer.infos_.overviews.size() == 1);
    const auto& block = viewer.infos_.overviews.front().texts.front();
    REQUIRE(block.posTexts.size() == 13);  // every score plus the one dimensions caption
    StreamViewerOverview::OverviewInfo info;
    info.texts.push_back(block);
    viewer.FrameOverview(nullptr, info);
    CHECK(osd.texts.size() == 25);  // no historical seven-row truncation
    CHECK(std::find(osd.texts.begin(), osd.texts.end(), "raw") != osd.texts.end());
    CHECK(std::find(osd.texts.begin(), osd.texts.end(), "raw extended") != osd.texts.end());
    CHECK(std::find(osd.texts.begin(), osd.texts.end(), "item9") != osd.texts.end());

    osd.texts.clear();
    viewer.height_ = 100;
    viewer.FrameOverview(nullptr, info);
    CHECK(std::find(osd.texts.begin(), osd.texts.end(), "+10") != osd.texts.end());
    CHECK(viewer.infos_.overviews.front().texts.front().posTexts.size() == 13);
    const auto panel = osd.roundedRects.back();
    CHECK(panel.y >= 0);
    CHECK(panel.y + panel.height <= 100);
}

TEST_CASE("Fine label clipping preserves Unicode boundaries and separate numeric fields",
          "[stream][osd][text-layout]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_  = 300;
    viewer.height_ = 180;
    MsgTarget target{};
    target.trackId        = 7;
    target.aiBox          = {295, 160, 40, 60};
    const std::string raw = "原始标签很长但是百分号99%和井号#literal并不是分数字段";
    target.confidence.push_back({raw, 0.92F, true});
    viewer.LiveDataHandTarget(0, 1, 1000, target);
    StreamViewerOverview::OverviewInfo info;
    info.texts = viewer.infos_.overviews.front().texts;
    viewer.FrameOverview(nullptr, info);
    REQUIRE(osd.texts.size() == 4);
    CHECK(osd.texts[0].find("...") != std::string::npos);
    CHECK(osd.texts[1] == "#7");
    CHECK(osd.textFont[1] == 13);
    CHECK(osd.texts[2] == "92.0%");
    CHECK(nlohmann::json(osd.texts[0]).dump().size() > 0);  // serializer validates UTF-8
    CHECK(info.texts[0].posTexts[0].targetLabel->label == raw);
    for (size_t i = 0; i < osd.glyphs.size(); ++i) {
        CHECK(osd.glyphX[i] >= 0);
        CHECK(osd.glyphX[i] + viewer.GetFineTextWidth(osd.glyphs[i], osd.glyphFont[i]) <= 300);
        CHECK(osd.glyphY[i] + osd.glyphFont[i] + 2 <= 180);
    }
}

TEST_CASE("Very narrow panels keep scores paired or explicitly omit them without clipping",
          "[stream][osd][text-layout]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_  = GENERATE(20, 34, 60, 110);
    viewer.height_ = 180;
    MsgTarget target{};
    target.trackId = 7;
    target.aiBox   = {3, 40, 20, 30};
    target.confidence.push_back({"person", 0.92F, true});
    viewer.LiveDataHandTarget(0, 1, 1000, target);
    StreamViewerOverview::OverviewInfo info;
    info.texts = viewer.infos_.overviews.front().texts;
    viewer.FrameOverview(nullptr, info);
    CHECK(info.texts.front().posTexts.front().targetLabel->label == "person");
    CHECK(*info.texts.front().posTexts.front().targetLabel->score == Catch::Approx(0.92F));
    if (viewer.width_ == 20) {
        CHECK(osd.roundedRects.empty());  // no room even for a legible omission glyph
        CHECK(osd.texts.empty());
    } else {
        REQUIRE(osd.texts.size() == 3);
        CHECK_FALSE(osd.texts[0].empty());
        CHECK(osd.texts[1] == (viewer.width_ == 110 ? "92.0%" : "…"));
        CHECK(osd.textY[1] > osd.textY[0]);  // paired two-line fallback within one panel
        for (size_t i = 0; i < osd.texts.size(); ++i) {
            CHECK(osd.textX[i] >= 0);
            CHECK(osd.textX[i] + viewer.GetFineTextWidth(osd.texts[i], osd.textFont[i]) <= viewer.width_);
            CHECK(osd.textY[i] + osd.textFont[i] + 2 <= viewer.height_);
        }
    }
}

TEST_CASE("Neighbouring target labels avoid boxes and each other deterministically",
          "[stream][osd][target-layout]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    std::vector<util::Box> boxes;
    int resultCount = 0;
    SECTION("Close targets use distinct panels without losing any result") {
        viewer.width_  = 640;
        viewer.height_ = 480;
        boxes          = {{250, 220, 49, 71}, {335, 235, 56, 82}, {375, 200, 49, 71}};
        resultCount    = 5;
    }
    SECTION("Labels near image corners remain within the frame") {
        viewer.width_  = 360;
        viewer.height_ = 240;
        boxes          = {{0, 0, 24, 40}, {320, 5, 35, 50}, {0, 190, 28, 50}};
        resultCount    = 2;
    }
    for (const auto& box : boxes) {
        MsgTarget target{};
        target.aiBox = {box.x, box.y, box.width, box.height};
        for (int i = 0; i < resultCount; ++i)
            target.confidence.push_back({"raw" + std::to_string(i), 0.5F, true});
        viewer.LiveDataHandTarget(0, 1, 1000, target);
    }
    StreamViewerOverview::OverviewInfo info;
    info.texts = viewer.infos_.overviews.front().texts;
    viewer.FrameOverview(nullptr, info);
    REQUIRE(osd.roundedRects.size() == 3);
    REQUIRE(osd.texts.size() == static_cast<size_t>(3 * (resultCount * 2 + 1)));
    auto intersects = [](const util::Box& a, const util::Box& b) {
        return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
    };
    std::vector<util::Box> labels = osd.roundedRects;
    for (size_t i = 0; i < osd.texts.size(); ++i) {
        if (osd.textFont[i] == 13)
            labels.push_back({osd.textX[i], osd.textY[i], viewer.GetFineTextWidth(osd.texts[i], 13), 15});
    }
    REQUIRE(labels.size() == 6);
    for (size_t i = 0; i < labels.size(); ++i) {
        const auto& label = labels[i];
        CHECK(label.x >= 0);
        CHECK(label.y >= 0);
        CHECK(label.x + label.width <= viewer.width_);
        CHECK(label.y + label.height <= viewer.height_);
        for (const auto& box : boxes) {
            CAPTURE(i, label.x, label.y, label.width, label.height, box.x, box.y, box.width, box.height);
            CHECK_FALSE(intersects(label, box));
        }
        for (size_t j = i + 1; j < labels.size(); ++j) {
            CAPTURE(i, j, label.x, label.y, label.width, label.height, labels[j].x, labels[j].y,
                    labels[j].width, labels[j].height);
            CHECK_FALSE(intersects(label, labels[j]));
        }
    }
    const auto originalText = osd.texts;
    const auto originalX    = osd.textX;
    const auto originalY    = osd.textY;
    viewer.FrameOverview(nullptr, info);
    for (size_t i = 0; i < originalText.size(); ++i) {
        CHECK(osd.texts[i + originalText.size()] == originalText[i]);
        CHECK(osd.textX[i + originalText.size()] == originalX[i]);
        CHECK(osd.textY[i + originalText.size()] == originalY[i]);
    }
}

TEST_CASE("A clear single-target composition keeps its approved coordinates",
          "[stream][osd][target-layout]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_  = 640;
    viewer.height_ = 480;
    MsgTarget target{};
    target.aiBox      = {250, 220, 49, 71};
    target.confidence = {{"raw", 0.5F, true}, {"extra", 0.4F, true}};
    viewer.LiveDataHandTarget(0, 1, 1000, target);
    StreamViewerOverview::OverviewInfo info;
    info.texts = viewer.infos_.overviews.front().texts;
    viewer.FrameOverview(nullptr, info);
    REQUIRE(osd.roundedRects.size() == 1);
    const auto panel = osd.roundedRects.front();
    CHECK(panel.x == 250 + 49 + 13);
    CHECK(panel.y == 220 - 9);
    REQUIRE(osd.texts.size() == 5);
    CHECK(osd.textX.back() == 250);
    CHECK(osd.textY.back() == 297);
}

TEST_CASE("A full image uses bounded placement without deleting target content",
          "[stream][osd][target-layout]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_  = 180;
    viewer.height_ = 100;
    for (int i = 0; i < 2; ++i) {
        MsgTarget target{};
        target.aiBox = {i, i, 179 - i, 99 - i};
        target.confidence.push_back({i == 0 ? "A" : "B", 0.5F, true});
        viewer.LiveDataHandTarget(0, 1, 1000, target);
    }
    StreamViewerOverview::OverviewInfo info;
    info.texts = viewer.infos_.overviews.front().texts;
    viewer.FrameOverview(nullptr, info);
    REQUIRE(osd.roundedRects.size() == 2);
    REQUIRE(osd.texts.size() == 6);
    CHECK(osd.texts[0] == "A");
    CHECK(osd.texts[1] == "50.0%");
    CHECK(osd.texts[3] == "B");
    CHECK(osd.texts[4] == "50.0%");
    for (const auto& panel : osd.roundedRects) {
        CHECK(panel.x >= 0);
        CHECK(panel.y >= 0);
        CHECK(panel.x + panel.width <= viewer.width_);
        CHECK(panel.y + panel.height <= viewer.height_);
    }
    CHECK(info.texts.size() == 2);
}

TEST_CASE("Live OSD distinguishes oriented enclosing dimensions and measured box size",
          "[stream][osd][geometry]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_                      = 640;
    viewer.height_                     = 360;
    viewer.frame_identity_.streamIndex = 1;

    MsgTarget target{};
    target.trackId = 5;
    target.aiBox   = {100, 90, 120, 200};
    target.confidence.push_back({"person", 0.9F, true});
    viewer.LiveDataHandTarget(1, 8, 1000, target);
    target.aiBox = {100, 90, 140, 220};
    viewer.LiveDataHandTarget(1, 9, 1040, target);
    const auto& nextText = viewer.infos_.overviews.back().texts.front().posTexts;
    REQUIRE(nextText.size() == 2);
    CHECK(nextText[1].text == "140x220 px");
    // The target geometry being smoothed must not alter its measured dimensions.
    CHECK(viewer.smoothed_boxes_.at(5).w != 140.0F);
    CHECK(target.aiBox.width == 140);

    target.trackId          = -1;
    target.oriented_corners = util::Quad{{{100, 90}, {230, 100}, {240, 300}, {90, 290}}};
    viewer.LiveDataHandTarget(1, 10, 1080, target);
    const auto& orientedText = viewer.infos_.overviews.back().texts.front().posTexts;
    REQUIRE(orientedText.size() == 2);
    CHECK(orientedText[1].text == "AABB 140x220 px");
}

TEST_CASE("Target IDs are independently styled and score columns do not resize with values",
          "[stream][osd][target-panel]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    CapturingOSD osd;
    test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration(osd);
    WidthRenderer renderer;
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration(renderer);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_  = 640;
    viewer.height_ = 360;
    StreamOverviewText block;
    block.targetBox = util::Box{100, 120, 49, 71};
    StreamOverviewTextEl label;
    label.targetLabel = StreamOverviewTextEl::TargetLabel{"raw #x", .099F, 0};
    block.posTexts.push_back(label);
    StreamViewerOverview::OverviewInfo info;
    info.texts.push_back(block);
    viewer.FrameOverview(nullptr, info);
    REQUIRE(osd.texts.size() == 3);
    CHECK(osd.texts[0] == "raw #x");
    CHECK(osd.texts[1] == "#0");
    CHECK(osd.textFont[1] == 13);
    REQUIRE(osd.glyphs[1] == "#0");
    CHECK(osd.glyphColor[1].red == 220);
    CHECK(osd.glyphColor[1].green == 212);
    CHECK(osd.glyphColor[1].blue == 238);
    CHECK(osd.texts[2] == "9.9%");
    REQUIRE(osd.outlines.size() == 2);  // panel and independent ID marker
    const auto width   = osd.roundedRects.back().width;
    const auto percent = std::find(osd.glyphs.begin(), osd.glyphs.end(), "%") - osd.glyphs.begin();
    REQUIRE(static_cast<size_t>(percent) < osd.glyphs.size());
    const int percentX = osd.glyphX[percent];
    CHECK(osd.glyphFont[percent] == 12);
    info.texts[0].posTexts[0].targetLabel->score = 1.F;
    viewer.FrameOverview(nullptr, info);
    CHECK(osd.roundedRects.back().width == width);
    CHECK(osd.texts.back() == "100.0%");
    CHECK(osd.glyphs.back() == "%");
    CHECK(osd.glyphX.back() == percentX);
    CHECK(info.texts[0].posTexts[0].targetLabel->label == "raw #x");
    CHECK(*info.texts[0].posTexts[0].targetLabel->score == Catch::Approx(1.F));
}

TEST_CASE("Tracked target panels retain their anchor through transient collisions",
          "[stream][osd][target-stability]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_          = 640;
    viewer.height_         = 480;
    viewer.frame_identity_ = {1, 1, 1000};
    viewer.PrepareTargetLayoutState();
    const util::Box target{100, 150, 49, 71};
    const util::Box preferred{162, 141, 166, 44};
    const auto original = viewer.StabilizeTargetPanel(7, preferred, target, {target}, {});
    CHECK(original.x == preferred.x);
    CHECK(original.y == preferred.y);
    const std::vector<util::Box> obstacles{target, preferred};
    viewer.frame_identity_.timestamp = 1100;
    viewer.PrepareTargetLayoutState();
    auto held = viewer.StabilizeTargetPanel(7, preferred, target, obstacles, {});
    CHECK(held.x == original.x);
    CHECK(held.y == original.y);
    viewer.frame_identity_.timestamp = 1299;
    viewer.PrepareTargetLayoutState();
    held = viewer.StabilizeTargetPanel(7, preferred, target, obstacles, {});
    CHECK(held.x == original.x);
    CHECK(held.y == original.y);
    viewer.frame_identity_.timestamp = 1300;
    viewer.PrepareTargetLayoutState();
    const auto moved = viewer.StabilizeTargetPanel(7, preferred, target, obstacles, {});
    CHECK((moved.x != original.x || moved.y != original.y));
    CHECK_FALSE((moved.x < preferred.x + preferred.width && moved.x + moved.width > preferred.x &&
                 moved.y < preferred.y + preferred.height && moved.y + moved.height > preferred.y));
    // Removing the obstacle does not make the card immediately jump back.
    viewer.frame_identity_.timestamp = 1400;
    viewer.PrepareTargetLayoutState();
    held = viewer.StabilizeTargetPanel(7, preferred, target, {target}, {});
    CHECK(held.x == moved.x);
    CHECK(held.y == moved.y);
    // A target translation moves its card by the same delta.
    const util::Box translated{110, 160, 49, 71};
    held = viewer.StabilizeTargetPanel(7, {172, 151, 166, 44}, translated, {translated}, {});
    CHECK(held.x == moved.x + 10);
    CHECK(held.y == moved.y + 10);
}

TEST_CASE("Target placement history expires and cannot leak across streams or dimensions",
          "[stream][osd][target-stability]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_          = 640;
    viewer.height_         = 480;
    viewer.frame_identity_ = {1, 1, 1000};
    viewer.PrepareTargetLayoutState();
    const util::Box target{100, 150, 49, 71}, preferred{162, 141, 166, 44};
    viewer.StabilizeTargetPanel(-1, preferred, target, {target}, {});
    CHECK(viewer.target_panel_states_.empty());
    viewer.StabilizeTargetPanel(7, preferred, target, {target}, {});
    REQUIRE(viewer.target_panel_states_.size() == 1);
    SECTION("Expiry is based on elapsed time, including frames without detections") {
        viewer.frame_identity_.timestamp = 4001;
    }
    SECTION("A changed stream invalidates reused numeric IDs") {
        viewer.frame_identity_.streamIndex = 2;
    }
    SECTION("Resolution change invalidates placement geometry") {
        viewer.width_ = 320;
    }
    SECTION("Timestamp rewind invalidates replayed identity") {
        viewer.frame_identity_.timestamp = 999;
    }
    viewer.PrepareTargetLayoutState();
    CHECK(viewer.target_panel_states_.empty());
}

TEST_CASE("Target placement history is bounded and offscreen cards move immediately",
          "[stream][osd][target-stability]") {
    test::MockTaskService tasks;
    ALLOW_CALL(tasks, GetTaskFrameInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_))
        .RETURN(true);
    test::ScopedServiceOverride<service::ITaskQuery> registration(tasks);
    StreamViewerOverview viewer("camera", "algorithm");
    viewer.width_          = 640;
    viewer.height_         = 480;
    viewer.frame_identity_ = {1, 1, 1000};
    viewer.PrepareTargetLayoutState();
    const util::Box target{100, 150, 49, 71}, preferred{162, 141, 166, 44};
    for (int id = 0; id < 300; ++id)
        viewer.StabilizeTargetPanel(id, preferred, target, {target}, {});
    CHECK(viewer.target_panel_states_.size() == viewer.kMaxTargetPanelStates);
    CHECK(viewer.target_panel_states_.count(0) == 0);
    CHECK(viewer.target_panel_states_.count(299) == 1);
    const util::Box edge{590, 150, 49, 71};
    auto panel = viewer.StabilizeTargetPanel(299, {470, 141, 166, 44}, edge, {edge}, {});
    CHECK(panel.x >= 4);
    CHECK(panel.x + panel.width <= viewer.width_ - 4);
    CHECK(panel.y >= 4);
    CHECK(panel.y + panel.height <= viewer.height_ - 4);
}

}  // namespace
