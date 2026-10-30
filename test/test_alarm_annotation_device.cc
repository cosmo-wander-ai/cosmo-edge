#include "catch_amalgamated.hpp"

#ifdef COSMO_MEDIA_USE_SOPHON_BACKEND
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "bmlib_runtime.h"
#include "flow/alarm/AlarmImageAnnotations.h"
#include "media/Color.h"
#include "media/EncodedImageInfo.h"
#include "media/OsdTextRenderer.h"
#include "media/VideoFrame.h"
#include "mem/AllocatorSophon.h"
#include "mem/DeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "service/model/dto/ModelInfo.h"
#include "support/ScopedServiceOverride.h"
#include "util/AiTypes.h"

namespace {

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;

// This hidden test never starts the engine or opens a model. Explicit paths keep
// the real font read-only and every generated JPEG inside the caller's private run.
std::filesystem::path MakeArtifactDirectory(const char* configured) {
    REQUIRE(configured != nullptr);
    const std::filesystem::path parent(configured);
    REQUIRE(parent.is_absolute());
    const auto status = std::filesystem::symlink_status(parent);
    REQUIRE(std::filesystem::is_directory(status));
    const auto public_permissions = std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    REQUIRE((status.permissions() & public_permissions) == std::filesystem::perms::none);
    const auto pattern = (parent / "alarm-annotations-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    const auto* created = ::mkdtemp(name.data());
    REQUIRE(created != nullptr);
    return created;
}

class AnnotationDeviceFixture {
public:
    explicit AnnotationDeviceFixture(const std::string& font)
        : device_registration_(device_),
          renderer_registration_(renderer_),
          pool_(std::make_unique<cosmo::mem::AllocatorSophon>(device_), {kWidth * kHeight * 3}) {
        REQUIRE(renderer_.Init(font));
        REQUIRE(renderer_.IsReady());
        // A loaded Latin-only font must not pass with identical missing-glyph boxes.
        const auto missing = renderer_.RenderString("\xF4\x8F\xBF\xBF", 24.f);
        for (const auto* glyph : {"火", "焰", "烟", "雾"}) {
            const auto bitmap = renderer_.RenderString(glyph, 24.f);
            REQUIRE(bitmap.width > 0);
            REQUIRE(bitmap.height > 0);
            REQUIRE(std::any_of(bitmap.alpha.begin(), bitmap.alpha.end(), [](uint8_t a) { return a != 0; }));
            REQUIRE(bitmap.alpha != missing.alpha);
        }
        cosmo::mem::SetMemoryPoolContext(&pool_);
    }

    ~AnnotationDeviceFixture() {
        cosmo::mem::SetMemoryPoolContext(nullptr);
    }

    VideoFramePtr Frame() {
        auto frame = std::make_shared<cosmo::media::VideoFrame>(kWidth, kHeight,
                                                                cosmo::media::PixelFormat::PIXEL_I420);
        REQUIRE(frame->Active());
        // Sophon drawing operates on I420. Keep both chroma planes neutral and
        // vary only Y so before/after assertions inspect the real luminance pixels.
        std::vector<uint8_t> pixels(frame->GetSize(), 128);
        REQUIRE(pixels.size() == static_cast<size_t>(kWidth * kHeight * 3 / 2));
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const auto offset = static_cast<size_t>(y * kWidth + x);
                pixels[offset]    = static_cast<uint8_t>(48 + (x / 32 + y / 32) % 48);
            }
        }
        auto* memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
        REQUIRE(memory != nullptr);
        REQUIRE(bm_memcpy_s2d_partial(reinterpret_cast<bm_handle_t>(device_.GetMediaHandle()), *memory,
                                      pixels.data(), static_cast<unsigned int>(pixels.size())) == BM_SUCCESS);
        return frame;
    }

    std::vector<uint8_t> Download(const VideoFramePtr& frame) {
        std::vector<uint8_t> pixels(frame->GetSize());
        auto* memory = reinterpret_cast<bm_device_mem_t*>(frame->GetData());
        REQUIRE(memory != nullptr);
        REQUIRE(bm_memcpy_d2s_partial(reinterpret_cast<bm_handle_t>(device_.GetMediaHandle()), pixels.data(),
                                      *memory, static_cast<unsigned int>(pixels.size())) == BM_SUCCESS);
        return pixels;
    }

    cosmo::media::OsdTextRenderer& Renderer() {
        return renderer_;
    }
    cosmo::service::VideoFrameServiceImpl& Video() {
        return video_;
    }

private:
    cosmo::mem::DeviceContext device_;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> device_registration_;
    cosmo::media::OsdTextRenderer renderer_;
    cosmo::test::ScopedServiceOverride<cosmo::media::IOsdTextRenderer> renderer_registration_;
    cosmo::mem::MemoryPoolMng pool_;
    cosmo::service::VideoFrameServiceImpl video_;
};

void RequireRenderedText(AnnotationDeviceFixture& fixture,
                         const std::vector<cosmo::alarm::AlarmImageText>& layout,
                         const std::vector<uint8_t>& before, const std::vector<uint8_t>& after) {
    REQUIRE(before.size() == after.size());
    REQUIRE(before.size() == static_cast<size_t>(kWidth * kHeight * 3 / 2));
    for (const auto& text : layout) {
        INFO("Rendered annotation: " << text.text);
        const auto bitmap = fixture.Renderer().RenderStringWithOutline(
            text.text, text.fontSize * cosmo::alarm::AlarmImageTextRenderScale());
        REQUIRE(bitmap.width > 0);
        REQUIRE(bitmap.height > 0);
        size_t changed = 0;
        // Include the shared helper's background padding and outlined glyphs.
        for (int y = std::max(0, text.y - 8); y < std::min(kHeight, text.y + bitmap.height + 8); ++y) {
            for (int x = std::max(0, text.x - 8); x < std::min(kWidth, text.x + bitmap.width + 8); ++x) {
                const auto offset = static_cast<size_t>(y * kWidth + x);
                if (before[offset] != after[offset])
                    ++changed;
            }
        }
        REQUIRE(changed > 10);
    }
}

nlohmann::json WriteSample(AnnotationDeviceFixture& fixture, const std::filesystem::path& directory,
                           const std::string& name,
                           const std::vector<cosmo::alarm::AlarmImageTargetText>& targets, bool showSize) {
    auto frame = fixture.Frame();
    std::vector<cosmo::util::Box> boxes;
    for (const auto& target : targets)
        boxes.push_back(target.box);
    if (!boxes.empty()) {
        frame = fixture.Video().DrawRects(frame, boxes, cosmo::media::Color{255, 0, 0}, 2);
        REQUIRE(VideoFrameValid(frame));
    }
    const auto before = fixture.Download(frame);
    const auto layout = cosmo::alarm::BuildAlarmImageTextLayout(
        targets, kWidth, kHeight, showSize, fixture.Renderer(), cosmo::alarm::AlarmImageTextRenderScale());
    REQUIRE(cosmo::alarm::DrawAlarmImageText(fixture.Video(), frame, layout));
    const auto after = fixture.Download(frame);
    if (targets.empty()) {
        REQUIRE(layout.empty());
        REQUIRE(before == after);
    } else {
        REQUIRE_FALSE(layout.empty());
        RequireRenderedText(fixture, layout, before, after);
    }

    auto texts = nlohmann::json::array();
    std::string all_text;
    for (const auto& text : layout) {
        texts.push_back({{"text", text.text}, {"x", text.x}, {"y", text.y}, {"fontSize", text.fontSize}});
        all_text += text.text + "\n";
    }
    if (!targets.empty()) {
        REQUIRE(all_text.find("火焰 92.3%") != std::string::npos);
        REQUIRE(all_text.find("烟雾 81.7%") != std::string::npos);
        REQUIRE((all_text.find("260x180") != std::string::npos) == showSize);
        REQUIRE((all_text.find("300x220") != std::string::npos) == showSize);
    }
    const auto jpeg = fixture.Video().EncodeJpeg(frame, 95);
    REQUIRE_FALSE(jpeg.empty());
    cosmo::media::EncodedImageInfo info;
    REQUIRE(cosmo::media::InspectEncodedImage(jpeg, info));
    REQUIRE(info.width == kWidth);
    REQUIRE(info.height == kHeight);
    std::ofstream output(directory / name, std::ios::binary);
    output.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
    output.close();
    REQUIRE(output.good());
    return {{"file", name},          {"width", info.width},
            {"height", info.height}, {"jpegBytes", jpeg.size()},
            {"showSize", showSize},  {"annotations", std::move(texts)}};
}

}  // namespace

TEST_CASE("Sophon alarm annotation helper renders Chinese labels and sizes into reviewable JPEGs",
          "[.device][alarm-annotation-device]") {
    const auto* enabled = std::getenv("COSMO_RUN_ALARM_ANNOTATION_DEVICE");
    if (!enabled || std::string(enabled) != "1") {
        SKIP("Set COSMO_RUN_ALARM_ANNOTATION_DEVICE=1 with explicit font and private artifact paths");
    }
    const auto* font = std::getenv("COSMO_ALARM_ANNOTATION_FONT");
    REQUIRE(font != nullptr);
    REQUIRE(std::filesystem::path(font).is_absolute());
    REQUIRE(std::filesystem::is_regular_file(font));
    const auto directory = MakeArtifactDirectory(std::getenv("COSMO_ALARM_ANNOTATION_OUTPUT_DIR"));
    INFO("Annotation artifacts: " << directory.string());
    AnnotationDeviceFixture fixture(font);
    // No model weights are needed: use explicit test metadata and the same exact
    // resource keys as the preset catalog, then exercise production translation.
    const auto preset_root = directory / "models";
    REQUIRE(std::filesystem::create_directories(preset_root / "2001003"));
    cosmo::ModelInfo model;
    model.id                          = "2001003";
    model.path                        = (preset_root / model.id).string();
    model.labels                      = {{"fire", "fire", "0"}, {"smog", "smog", "1"}};
    const nlohmann::json translations = {{"resource.label.2001003.fire.2001003.0.fire.namecn", "火焰"},
                                         {"resource.label.2001003.smog.2001003.1.smog.namecn", "烟雾"}};
    const auto display                = [&](const std::string& raw, float score) {
        cosmo::AiTargetAnnotation annotation;
        annotation.label       = raw;
        annotation.atomic_code = model.id;
        annotation.score       = score;
        annotation.source      = cosmo::AiAnnotationSource::Detection;
        return cosmo::alarm::FormatAlarmTargetLabel(
            cosmo::alarm::ResolveAlarmTargetLabel(annotation, model, preset_root.string(), translations),
            annotation.score);
    };
    const std::vector<cosmo::alarm::AlarmImageTargetText> targets = {
        {{90, 170, 260, 180}, {display("fire", 0.923F)}}, {{610, 350, 300, 220}, {display("smog", 0.817F)}}};
    nlohmann::json samples = nlohmann::json::array();
    samples.push_back(WriteSample(fixture, directory, "labels-and-sizes.jpg", targets, true));
    samples.push_back(WriteSample(fixture, directory, "labels-only.jpg", targets, false));
    samples.push_back(WriteSample(fixture, directory, "no-target-annotations.jpg", {}, false));
    const nlohmann::json manifest = {
        {"scope", "production annotation helper and real Sophon OSD/JPEG; no inference or event publication"},
        {"promptRemovalCoverage",
         "TaskAlarmPicture integration is tested separately; this helper takes no prompt"},
        {"font", std::filesystem::path(font).filename().string()},
        {"samples", std::move(samples)}};
    std::ofstream output(directory / "manifest.json");
    output << manifest.dump(2) << '\n';
    output.close();
    REQUIRE(output.good());
    SUCCEED("Reviewable annotation JPEGs and manifest written to " << directory.string());
}
#endif
