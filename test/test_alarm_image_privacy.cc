#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "util/AlarmImagePrivacy.h"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include "flow/alarm/AlarmImagePrivacy.h"
#include "flow/common/AlarmPrivacySnapshot.h"
#include "flow/common/AlgDataUnit.h"
#include "media/IOsdTextRenderer.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#endif

namespace cosmo {

TEST_CASE("Alarm image privacy defaults preserve existing behavior", "[privacy][policy]") {
    util::AlarmImagePrivacy policy;
    CHECK(policy.Valid());
    CHECK_FALSE(policy.Requested());
    CHECK(policy.Selects("person"));
    CHECK(policy.Strength() == 2);
    CHECK_FALSE(policy.Apply("param.privacyEnabledExtra", "1"));
    CHECK_FALSE(policy.Requested());
    REQUIRE(policy.Apply(util::kPrivacyEnabled, "1"));
    CHECK(policy.Requested());
}

TEST_CASE("Alarm image privacy labels match complete tokens", "[privacy][policy]") {
    util::AlarmImagePrivacy policy;
    REQUIRE(policy.Apply(util::kPrivacyLabels, "person,person_child,car"));
    CHECK(policy.Valid());
    CHECK(policy.Selects("person"));
    CHECK(policy.Selects("person_child"));
    CHECK(policy.Selects("car"));
    CHECK_FALSE(policy.Selects("per"));
    CHECK_FALSE(policy.Selects("person_child_extra"));
    CHECK_FALSE(policy.Selects(""));
    REQUIRE(policy.Apply(util::kPrivacyLabels, "person_child"));
    CHECK_FALSE(policy.Selects("person"));
}

TEST_CASE("Malformed privacy parameters cannot silently disable masking", "[privacy][policy]") {
    for (const std::string value : {"", "true", "false", "2", "0 ", " 0"}) {
        util::AlarmImagePrivacy policy;
        REQUIRE(policy.Apply(util::kPrivacyEnabled, value));
        CHECK_FALSE(policy.Valid());
        CHECK(policy.Requested());
    }
    for (const std::string value : {"", "0", "4", "2.0", "02", " 2"}) {
        util::AlarmImagePrivacy policy;
        REQUIRE(policy.Apply(util::kPrivacyStrength, value));
        CHECK_FALSE(policy.Valid());
    }
    for (const std::string value :
         {"", ",person", "person,", "person,,car", "person, car", "person\t", "person,*", "*person"}) {
        util::AlarmImagePrivacy policy;
        REQUIRE(policy.Apply(util::kPrivacyLabels, value));
        CHECK_FALSE(policy.Valid());
    }
    for (int strength = 1; strength <= 3; ++strength) {
        util::AlarmImagePrivacy policy;
        REQUIRE(policy.Apply(util::kPrivacyStrength, std::to_string(strength)));
        CHECK(policy.Valid());
        CHECK(policy.Strength() == strength);
    }
    MsgDynamicKeyValue enabled;
    enabled.key   = std::string(util::kPrivacyEnabled);
    enabled.value = "yes";
    CHECK_FALSE(util::ValidateAlarmImagePrivacyParams({enabled}));
    enabled.value = "1";
    CHECK(util::ValidateAlarmImagePrivacyParams({enabled}));
}

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
namespace {

    class PrivacyDeviceContext final : public mem::IDeviceContext {
    public:
        void* GetMemoryHandle() override {
            return nullptr;
        }
        void* GetMediaHandle() override {
            return nullptr;
        }
    };

    class PrivacyTextRenderer final : public media::IOsdTextRenderer {
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

    class PrivacyImageFixture {
    public:
        PrivacyImageFixture()
            : pool_(std::make_unique<mem::AllocatorCpu>(), {64 * 64 * 3}),
              device_registration_(device_),
              text_registration_(renderer_),
              transform_registration_(service_),
              osd_registration_(service_) {
            mem::SetMemoryPoolContext(&pool_);
        }
        ~PrivacyImageFixture() {
            mem::SetMemoryPoolContext(nullptr);
        }

        VideoFramePtr Frame() {
            auto frame =
                std::make_shared<media::VideoFrame>(64, 64, media::PixelFormat::PIXEL_RGB8, 17, 12345);
            REQUIRE(frame->Active());
            frame->SetStreamIndex(3);
            for (size_t offset = 0; offset < frame->GetSize(); ++offset) {
                frame->GetData()[offset] = static_cast<uint8_t>(offset % 251);
            }
            return frame;
        }

        void Publish(const VideoFramePtr& frame, std::vector<AlarmPrivacyTarget> targets,
                     const std::string& task = "privacy_test_task") {
            AlgData data;
            data.taskId            = task;
            data.chanDataDec.frame = frame;
            DataDetTrackClassify result;
            result.frameIndex  = static_cast<int64_t>(frame->GetFrameIndex());
            result.streamIndex = frame->GetStreamIndex();
            result.timestamp   = frame->GetTimestamp();
            result.picWidth    = static_cast<int>(frame->GetWidth());
            result.picHeight   = static_cast<int>(frame->GetHeight());
            for (const auto& target : targets) {
                AiDetectRstEl element;
                element.confidence.label = target.label;
                element.box              = target.box;
                // Privacy includes targets excluded by downstream business rules.
                element.bFilter = true;
                result.targets.push_back(element);
            }
            CaptureAlarmPrivacySnapshot(data, "test_detector", {"person", "person_child"}, result);
            REQUIRE(data.alarmPrivacySnapshot);
            PublishAlarmPrivacySnapshot(data);
        }

    private:
        mem::MemoryPoolMng pool_;
        PrivacyDeviceContext device_;
        PrivacyTextRenderer renderer_;
        test::ScopedServiceOverride<mem::IDeviceContext> device_registration_;
        test::ScopedServiceOverride<media::IOsdTextRenderer> text_registration_;
        service::VideoFrameServiceImpl service_;
        test::ScopedServiceOverride<service::IVideoFrameTransform> transform_registration_;
        test::ScopedServiceOverride<service::IVideoFrameOSD> osd_registration_;
    };

}  // namespace

TEST_CASE("Alarm images fail closed for missing, stale or unusable privacy metadata",
          "[privacy][alarm-image][cpu]") {
    PrivacyImageFixture fixture;
    auto frame = fixture.Frame();
    const std::vector<uint8_t> original(frame->GetData(), frame->GetData() + frame->GetSize());
    util::AlarmImagePrivacy policy;
    policy.enabled = "1";

    SECTION("missing detection results are not equivalent to zero targets") {
        CHECK(PrepareAlarmImage(frame, "privacy_test_task", policy) == nullptr);
    }
    SECTION("a different task cannot reuse the snapshot") {
        fixture.Publish(frame, {});
        CHECK(PrepareAlarmImage(frame, "other_task", policy) == nullptr);
    }
    SECTION("an explicitly selected class must have been detected on this frame") {
        fixture.Publish(frame, {});
        policy.labels = "face";
        CHECK(PrepareAlarmImage(frame, "privacy_test_task", policy) == nullptr);
    }
    SECTION("a changed frame identity rejects old coordinates") {
        fixture.Publish(frame, {{"person", {12, 12, 8, 8}}});
        frame->SetFrameIndex(18);
        CHECK(PrepareAlarmImage(frame, "privacy_test_task", policy) == nullptr);
    }
    SECTION("an invalid selected box omits the image after no source mutation") {
        fixture.Publish(frame, {{"person", {12, 12, 8, 8}}, {"person", {64, 0, 8, 8}}});
        CHECK(PrepareAlarmImage(frame, "privacy_test_task", policy) == nullptr);
    }
    SECTION("a corrupt enabled value cannot expose the original") {
        fixture.Publish(frame, {});
        policy.enabled = "true";
        CHECK(PrepareAlarmImage(frame, "privacy_test_task", policy) == nullptr);
    }
    CHECK(std::equal(original.begin(), original.end(), frame->GetData()));
}

TEST_CASE("A successful zero-target detection produces an independent alarm image",
          "[privacy][alarm-image][cpu]") {
    PrivacyImageFixture fixture;
    auto frame = fixture.Frame();
    fixture.Publish(frame, {});
    util::AlarmImagePrivacy policy;
    policy.enabled = "1";
    policy.labels  = "person";
    auto prepared  = PrepareAlarmImage(frame, "privacy_test_task", policy);
    REQUIRE(prepared);
    CHECK(prepared != frame);
    CHECK(prepared->GetData() != frame->GetData());
    CHECK(prepared->GetFrameIndex() == frame->GetFrameIndex());
    CHECK(prepared->GetStreamIndex() == frame->GetStreamIndex());
    CHECK(std::equal(frame->GetData(), frame->GetData() + frame->GetSize(), prepared->GetData()));
}

TEST_CASE("Alarm image selection masks the complete detected class with exact labels",
          "[privacy][alarm-image][cpu]") {
    PrivacyImageFixture fixture;
    auto frame = fixture.Frame();
    fixture.Publish(frame, {{"person", {12, 12, 8, 8}}, {"person_child", {40, 40, 8, 8}}});
    const std::vector<uint8_t> original(frame->GetData(), frame->GetData() + frame->GetSize());
    util::AlarmImagePrivacy policy;
    policy.enabled = "1";
    SECTION("explicit class selection keeps other classes unchanged") {
        policy.labels = "person";
        auto prepared = PrepareAlarmImage(frame, "privacy_test_task", policy);
        REQUIRE(prepared);
        CHECK(prepared->GetData()[(12 * 64 + 12) * 3] != original[(12 * 64 + 12) * 3]);
        for (int y = 38; y < 50; ++y) {
            for (int x = 38; x < 50; ++x) {
                const auto offset = (static_cast<size_t>(y) * 64 + x) * 3;
                CHECK(prepared->GetData()[offset] == original[offset]);
            }
        }
    }
    SECTION("all detected targets includes targets excluded by business filters") {
        auto prepared = PrepareAlarmImage(frame, "privacy_test_task", policy);
        REQUIRE(prepared);
        CHECK(prepared->GetData()[(12 * 64 + 12) * 3] != original[(12 * 64 + 12) * 3]);
        CHECK(prepared->GetData()[(40 * 64 + 40) * 3] != original[(40 * 64 + 40) * 3]);
    }
    CHECK(std::equal(original.begin(), original.end(), frame->GetData()));
}

TEST_CASE("Alarm images wait for the complete configured detector topology",
          "[privacy][alarm-image][cpu][topology]") {
    PrivacyImageFixture fixture;
    auto frame = fixture.Frame();
    AlgData data;
    data.taskId            = "privacy_topology_test";
    data.chanDataDec.frame = frame;
    DataDetTrackClassify result;
    result.streamIndex = frame->GetStreamIndex();
    result.frameIndex  = static_cast<int64_t>(frame->GetFrameIndex());
    result.timestamp   = frame->GetTimestamp();
    result.picWidth    = static_cast<int>(frame->GetWidth());
    result.picHeight   = static_cast<int>(frame->GetHeight());
    CaptureAlarmPrivacySnapshot(data, "detector-A", {"person"}, result);
    PublishAlarmPrivacySnapshot(data);

    util::AlarmImagePrivacy policy;
    policy.enabled = "1";
    // The first detector successfully found no targets, but the required parallel branch
    // has not run. Wildcard privacy must not release the untouched image at this point.
    CHECK_FALSE(PrepareAlarmImage(frame, data.taskId, policy, {"detector-A", "detector-B"}));
    CHECK_FALSE(PrepareAlarmImage(frame, data.taskId, policy, {"detector-A", "unsupported-detector"}));
    REQUIRE(PrepareAlarmImage(frame, data.taskId, policy, {"detector-A"}));

    CaptureAlarmPrivacySnapshot(data, "detector-B", {"vehicle"}, result);
    PublishAlarmPrivacySnapshot(data);
    auto complete = PrepareAlarmImage(frame, data.taskId, policy, {"detector-A", "detector-B"});
    REQUIRE(complete);
    CHECK(complete != frame);
}
#endif

}  // namespace cosmo
