#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/alarm/AlarmImageAnnotations.h"
#include "media/IOsdTextRenderer.h"
#include "nlohmann/json.hpp"
#include "service/media/IVideoFrameOSD.h"
#include "service/model/dto/ModelInfo.h"
#include "util/AiTypes.h"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include "flow/action/AlgActionBase.h"
#include "flow/alarm/AlarmVisualPlan.h"
#include "flow/alarm/LayaShadowObserver.h"
#include "flow/alarm/TaskAlarmSuppression.h"
#include "flow/channel/AlgChannel.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "flow/overview/OverviewRecordAlarmRst.h"
#include "flow/qwen3vl/OpenAiVlmClient.h"
#include "flow/task/TaskBaseParam.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockModelService.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/AlarmImagePrivacy.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/ClientMsgEvent.h"
#define private public
#include "flow/alarm/TaskAlarm.h"
#undef private
#endif

namespace cosmo {
namespace {
    class AnnotationMetrics final : public media::IOsdTextRenderer {
    public:
        bool ready{true};
        bool Init(const std::string&) override {
            return true;
        }
        bool IsReady() const override {
            return ready;
        }
        TextBitmap RenderString(const std::string& text, float pixels) const override {
            if (text.empty())
                return {};
            float width = 0;
            for (const unsigned char c : text) {
                if (c < 0x80)
                    width += pixels * 0.6F;
                else if ((c & 0xc0) != 0x80)
                    width += pixels;
            }
            const auto w = static_cast<int>(std::ceil(width));
            const auto h = static_cast<int>(std::ceil(pixels));
            return {std::vector<uint8_t>(static_cast<size_t>(w * h), 255), w, h};
        }
        OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
            return {};
        }
    };

    class AnnotationOSD final : public service::IVideoFrameOSD {
    public:
        bool accept{true};
        bool throwDraw{false};
        int begins{0};
        int ends{0};
        bool active{false};
        const media::VideoFrame* currentFrame{nullptr};
        std::vector<std::string> drawn;
        std::map<const media::VideoFrame*, std::vector<std::string>> frameText;
        VideoFramePtr CopyJpegSrcFrame(VideoFramePtr frame) override {
            return frame;
        }
        VideoFramePtr DrawLines(VideoFramePtr frame, std::vector<std::pair<util::Point, util::Point>>,
                                const media::Color&, int) override {
            return frame;
        }
        VideoFramePtr DrawRects(VideoFramePtr frame, const std::vector<util::Box>&, const media::Color&,
                                int) override {
            return frame;
        }
        VideoFramePtr DrawText(VideoFramePtr frame, int, int, const std::string&, const media::Color&,
                               int) override {
            FAIL("Alarm annotations must use the service-owned UTF-8 OSD session");
            return frame;
        }
        bool BeginOSD(VideoFramePtr frame) override {
            ++begins;
            active       = accept;
            currentFrame = frame.get();
            return accept;
        }
        void OSDDrawLines(std::vector<std::pair<util::Point, util::Point>>, const media::Color&,
                          int) override {}
        void OSDDrawText(int, int, const std::string&, const media::Color&, int) override {}
        void OSDDrawTextEx(int, int, const std::string& text, const media::Color&, int, const media::Color&,
                           uint8_t, bool, int) override {
            if (throwDraw)
                throw std::runtime_error("draw failed");
            drawn.push_back(text);
            frameText[currentFrame].push_back(text);
        }
        void EndOSD() override {
            ++ends;
            active = false;
        }
    };
}  // namespace

TEST_CASE("Alarm class translation requires exact preset source and model label identity",
          "[alarm][annotations][translation]") {
    AiTargetAnnotation annotation{"fire", "2001003", 0.923F, AiAnnotationSource::Detection};
    ModelInfo model;
    model.id   = "2001003";
    model.path = "/tmp/cosmo-annotation-presets/2001003";
    model.labels.push_back({"fire", "fire", "0"});
    const nlohmann::json translations = {{"resource.label.2001003.fire.2001003.0.fire.namecn", "火焰"}};
    const std::string root            = "/tmp/cosmo-annotation-presets";
    CHECK(alarm::ResolveAlarmTargetLabel(annotation, model, root, translations) == "火焰");
    SECTION("A user override with the same code and class keeps its raw name") {
        model.path = "/tmp/cosmo-annotation-users/2001003";
        CHECK(alarm::ResolveAlarmTargetLabel(annotation, model, root, translations) == "fire");
    }
    SECTION("A directory sharing only the prefix is not a preset") {
        model.path = root + "-other/2001003";
        CHECK(alarm::ResolveAlarmTargetLabel(annotation, model, root, translations) == "fire");
    }
    SECTION("The runtime model identity and the exact class ID must both match") {
        model.id = "other";
        CHECK(alarm::ResolveAlarmTargetLabel(annotation, model, root, translations) == "fire");
        model.id              = annotation.atomic_code;
        model.labels[0].label = "1";
        CHECK(alarm::ResolveAlarmTargetLabel(annotation, model, root, translations) == "fire");
    }
    SECTION("Missing metadata and unknown classes never receive guessed translations") {
        CHECK(alarm::ResolveAlarmTargetLabel(annotation, {}, root, translations) == "fire");
        annotation.label = "category1";
        CHECK(alarm::ResolveAlarmTargetLabel(annotation, model, root, translations) == "category1");
        annotation.label = "客户中文类别";
        CHECK(alarm::ResolveAlarmTargetLabel(annotation, model, root, translations) == "客户中文类别");
    }
}

TEST_CASE("Alarm score formatting displays measured probabilities and omits invalid scores",
          "[alarm][annotations]") {
    CHECK(alarm::FormatAlarmTargetLabel("火焰", 0.923F) == "火焰 92.3%");
    CHECK(alarm::FormatAlarmTargetLabel("火焰", 0.0F) == "火焰 0.0%");
    CHECK(alarm::FormatAlarmTargetLabel("火焰", 1.0F) == "火焰 100.0%");
    for (const auto score :
         {-1.0F, 1.1F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        CHECK(alarm::FormatAlarmTargetLabel("火焰", score) == "火焰");
    CHECK(alarm::FormatAlarmTargetLabel("火焰", std::nullopt) == "火焰");
    CHECK(alarm::FormatAlarmTargetLabel("", 0.9F).empty());
}

TEST_CASE("Alarm label layout preserves dimensions and stays within image edges at both renderer scales",
          "[alarm][annotations][layout]") {
    AnnotationMetrics renderer;
    const std::vector<alarm::AlarmImageTargetText> targets = {{{-5, 0, 20, 20}, {"火焰 92.3%"}},
                                                              {{315, 175, 5, 5}, {"烟雾 88.0%"}}};
    for (const float scale : {1.0F, 1.5F}) {
        const auto text = alarm::BuildAlarmImageTextLayout(targets, 320, 180, true, renderer, scale);
        REQUIRE(text.size() == 4);
        CHECK(text[0].text == "火焰 92.3%");
        CHECK(text[1].text == "20x20");
        CHECK(text[2].text == "烟雾 88.0%");
        CHECK(text[3].text == "5x5");
        for (const auto& line : text) {
            const auto metrics = renderer.RenderString(line.text, line.fontSize * scale);
            CHECK(line.x >= 5);
            CHECK(line.y >= 5);
            CHECK(line.x + metrics.width + 5 <= 320);
            CHECK(line.y + metrics.height + 5 <= 180);
        }
        CHECK(text[0].y + renderer.RenderString(text[0].text, text[0].fontSize * scale).height < text[1].y);
        CHECK(alarm::BuildAlarmImageTextLayout(targets, 320, 180, false, renderer, scale).size() == 2);
    }
}

TEST_CASE("Alarm layout bounds long UTF-8 labels and preserves their score suffix",
          "[alarm][annotations][layout]") {
    AnnotationMetrics renderer;
    const std::string longName = "这是一个很长很长的用户定义中文目标类别名称";
    const std::vector<alarm::AlarmImageTargetText> targets = {
        {{150, 50, 10, 10}, {alarm::FormatAlarmTargetLabel(longName, 0.923F)}}};
    const auto text = alarm::BuildAlarmImageTextLayout(targets, 180, 160, false, renderer, 1.0F);
    REQUIRE(text.size() == 1);
    CHECK(text.front().text.find("...") != std::string::npos);
    CHECK(text.front().text.find(" 92.3%") != std::string::npos);
    CHECK_NOTHROW(nlohmann::json(text.front().text).dump());
    CHECK(alarm::BuildAlarmImageTextLayout(targets, 8, 8, true, renderer, 1.0F).empty());
    renderer.ready = false;
    CHECK(alarm::BuildAlarmImageTextLayout(targets, 180, 160, true, renderer, 1.0F).empty());
}

TEST_CASE("Alarm text byte bounds and narrow images preserve the measured score",
          "[alarm][annotations][layout]") {
    AnnotationMetrics renderer;
    std::string longName;
    for (int i = 0; i < 100; ++i)
        longName += "中文";
    const auto formatted = alarm::FormatAlarmTargetLabel(longName, 0.923F);
    REQUIRE(formatted.size() < 256);
    REQUIRE(formatted.find("92.3%") != std::string::npos);
    const std::vector<alarm::AlarmImageTargetText> targets = {{{30, 30, 10, 10}, {formatted}}};
    for (const int width : {80, 120, 640}) {
        const auto text = alarm::BuildAlarmImageTextLayout(targets, width, 160, false, renderer, 1.0F);
        REQUIRE(text.size() == 1);
        CHECK(text.front().text.find("92.3%") != std::string::npos);
        CHECK_NOTHROW(nlohmann::json(text.front().text).dump());
        CHECK(text.front().x + renderer.RenderString(text.front().text, text.front().fontSize).width + 5 <=
              width);
    }
}

TEST_CASE("Alarm annotation sessions finish before encoding and release on drawing failure",
          "[alarm][annotations][osd]") {
    AnnotationOSD osd;
    const std::vector<alarm::AlarmImageText> text = {{"火焰 92.3%", 5, 5, 18}, {"20x20", 5, 30, 14}};
    REQUIRE(alarm::DrawAlarmImageText(osd, {}, text));
    CHECK(osd.begins == 1);
    CHECK(osd.ends == 1);
    CHECK(osd.drawn == std::vector<std::string>{"火焰 92.3%", "20x20"});
    osd.throwDraw = true;
    CHECK_THROWS_AS(alarm::DrawAlarmImageText(osd, {}, text), std::runtime_error);
    CHECK(osd.ends == 2);
    osd.accept = false;
    CHECK_FALSE(alarm::DrawAlarmImageText(osd, {}, text));
    CHECK(osd.ends == 2);
    CHECK(alarm::DrawAlarmImageText(osd, {}, {}));
    CHECK(osd.begins == 3);
}

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
namespace {
    class PictureDeviceContext final : public mem::IDeviceContext {
    public:
        void* GetMemoryHandle() override {
            return nullptr;
        }
        void* GetMediaHandle() override {
            return nullptr;
        }
    };

    class CapturingPictureCodec final : public service::IVideoFrameCodec {
    public:
        explicit CapturingPictureCodec(AnnotationOSD& value) : osd(value) {}
        struct Encoded {
            int quality;
            std::vector<std::string> text;
        };
        AnnotationOSD& osd;
        bool emitBytes{false};
        std::vector<Encoded> encoded;
        std::vector<u_char> EncodeJpeg(const VideoFramePtr frame, int quality) override {
            CHECK_FALSE(osd.active);  // Text must be flushed before JPEG encoding.
            encoded.push_back({quality, osd.frameText[frame.get()]});
            // Bytes are a test sentinel, not a rendered JPEG; device tests cover real encoding.
            return emitBytes ? std::vector<u_char>{0xff, 0xd8, 0xff, 0xd9} : std::vector<u_char>{};
        }
        VideoFramePtr DecodeJpeg(const std::vector<u_int8_t>&) override {
            return {};
        }
    };

    struct PictureFixture {
        mem::MemoryPoolMng pool{std::make_unique<mem::AllocatorCpu>(), {320 * 180 * 3}};
        PictureDeviceContext device;
        AnnotationMetrics renderer;
        AnnotationOSD osd;
        CapturingPictureCodec codec{osd};
        test::MockConfigReadService config;
        CfgAlarmParamOverviewInfo settings;
        test::ScopedServiceOverride<mem::IDeviceContext> deviceRegistration{device};
        test::ScopedServiceOverride<media::IOsdTextRenderer> rendererRegistration{renderer};
        service::VideoFrameServiceImpl frames;
        test::ScopedServiceOverride<service::IVideoFrameTransform> transformRegistration{frames};
        test::ScopedServiceOverride<service::IVideoFrameOSD> osdRegistration{osd};
        test::ScopedServiceOverride<service::IVideoFrameCodec> codecRegistration{codec};
        test::ScopedServiceOverride<service::IConfigReadService> configRegistration{config};

        PictureFixture() {
            mem::SetMemoryPoolContext(&pool);
            settings.picQuality         = 77;
            settings.targetBoxOverview  = true;
            settings.targetSizeOverview = true;
            settings.areaOverview       = false;
            settings.alarmTypeOverview  = false;
        }
        ~PictureFixture() {
            mem::SetMemoryPoolContext(nullptr);
        }
        VideoFramePtr Frame() {
            auto frame = std::make_shared<media::VideoFrame>(320, 180, media::PixelFormat::PIXEL_RGB8);
            REQUIRE(frame->Active());
            return frame;
        }
    };
}  // namespace

TEST_CASE("TaskAlarm pictures retain dimensions and JPEG settings without burning VLM prompts",
          "[alarm][annotations][picture][cpu]") {
    PictureFixture fixture;
    ALLOW_CALL(fixture.config, IsNetworkModel()).RETURN(false);
    ALLOW_CALL(fixture.config, GetPictureQuality()).LR_RETURN(fixture.settings);
    ActionNode action;
    action.flowActionId = "picture-annotations";
    TaskAlarm task("channel", "task", action);
    task.m_width             = 320;
    task.m_height            = 180;
    auto frame               = std::make_shared<AlgData>();
    frame->chanDataDec.frame = fixture.Frame();
    DataAlarmUnit unit;
    unit.box       = {60, 80, 40, 50};
    unit.ocrString = "private VLM prompt must not be drawn";
    unit.boxs.emplace_back(unit.box);
    unit.boxs.back().annotations.push_back({"火焰", "", 0.923F, AiAnnotationSource::Detection});
    // A synthetic event target is deliberately present; the picture must ignore it.
    CMsgOnEventsTarget synthetic;
    synthetic.label      = unit.ocrString;
    synthetic.confidence = 1.0F;
    unit.targets.push_back(synthetic);
    CMsgOnEventsReq event;
    event.messageId     = "annotation-test";
    event.itimestamp    = 1;
    event.algorithmName = "测试任务";

    SECTION("Normal target") {
        task.HandPicture(event, frame, unit);
        REQUIRE(fixture.codec.encoded.size() == 3);
        CHECK(fixture.codec.encoded[0].text.empty());
        CHECK(fixture.codec.encoded[1].text.empty());
        CHECK(fixture.codec.encoded[2].text == std::vector<std::string>{"火焰 92.3%", "40x50"});
        CHECK(fixture.codec.encoded[0].quality == 95);
        CHECK(fixture.codec.encoded[1].quality == 95);
        CHECK(fixture.codec.encoded[2].quality == 77);
    }
    SECTION("Size switch hides only dimensions") {
        fixture.settings.targetSizeOverview = false;
        task.HandPicture(event, frame, unit);
        CHECK(fixture.osd.drawn == std::vector<std::string>{"火焰 92.3%"});
    }
    SECTION("Box switch hides target labels and dimensions while title remains configurable") {
        fixture.settings.targetBoxOverview = false;
        fixture.settings.alarmTypeOverview = true;
        task.HandPicture(event, frame, unit);
        CHECK(fixture.osd.drawn == std::vector<std::string>{"测试任务"});
    }
    SECTION("Full-frame VLM keeps its existing decoration suppression") {
        unit.bLlmPrejudged = true;
        unit.box           = {0, 0, 320, 180};
        unit.boxs.clear();
        unit.boxs.emplace_back(unit.box);
        fixture.settings.alarmTypeOverview = true;
        task.HandPicture(event, frame, unit);
        CHECK(fixture.osd.drawn.empty());
    }
    SECTION("ROI VLM keeps the box dimensions without a synthetic label or confidence") {
        unit.bLlmPrejudged = true;
        unit.boxs[0].annotations.clear();
        task.HandPicture(event, frame, unit);
        CHECK(fixture.osd.drawn == std::vector<std::string>{"40x50"});
    }
    SECTION("Unknown annotation provenance cannot copy synthetic text onto the image") {
        unit.boxs[0].annotations = {{unit.ocrString, "", 1.0F, AiAnnotationSource::Unknown}};
        task.HandPicture(event, frame, unit);
        CHECK(fixture.osd.drawn == std::vector<std::string>{"40x50"});
    }
    SECTION("Optional model lookup failures preserve the class name and picture") {
        test::MockModelService models;
        test::ScopedServiceOverride<service::IModelQuery> modelRegistration{models};
        REQUIRE_CALL(models, GetModelInfo("unavailable")).THROW(std::runtime_error("lookup unavailable"));
        unit.boxs[0].annotations[0].atomic_code = "unavailable";
        task.HandPicture(event, frame, unit);
        REQUIRE(fixture.codec.encoded.size() == 3);
        CHECK(fixture.codec.encoded.back().text == std::vector<std::string>{"火焰 92.3%", "40x50"});
    }
    SECTION("Best snapshots use their frozen annotation after their original and crop are encoded") {
        const auto root = std::filesystem::temp_directory_path() /
                          ("cosmo-best-annotations-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        test::ScopedPathOverride paths((root / "data").string(), (root / "app").string());
        AiDetectBestEl best;
        best.bActive     = true;
        best.box         = {100, 70, 30, 60};
        best.bestFrame   = fixture.Frame();
        best.annotations = {{"人员", "", 0.812F, AiAnnotationSource::Detection}};
        unit.bestInfos.push_back(best);
        fixture.codec.emitBytes = true;
        task.HandBestInfoPicture(event, frame, unit, {}, {}, 77);
        REQUIRE(fixture.codec.encoded.size() == 3);
        CHECK(fixture.codec.encoded[0].text.empty());
        CHECK(fixture.codec.encoded[1].text.empty());
        CHECK(fixture.codec.encoded[2].text == std::vector<std::string>{"人员 81.2%", "30x60"});
        CHECK(fixture.codec.encoded[2].quality == 77);
    }
    for (const auto& line : fixture.osd.drawn)
        CHECK(line.find(unit.ocrString) == std::string::npos);
}
#endif
}  // namespace cosmo
