// Regression coverage for the I420 JPEG enrollment path and extraction errors.
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "infer/AiRecognizerUnify.h"
#include "media/Color.h"
#include "mock/MockModelService.h"
#include "service/face/impl/BodyLibServiceImpl.h"
#include "service/media/IVideoFrameTransform.h"
#include "support/ScopedFrameMemoryPool.h"
#include "support/ScopedServiceOverride.h"

namespace cosmo::test {
namespace {
    class EnrollmentFrameTransform : public service::IVideoFrameTransform {
    public:
        MAKE_MOCK2(Crop, VideoFramePtr(VideoFramePtr, util::Box), override);
        MAKE_MOCK3(Resize, VideoFramePtr(VideoFramePtr, int, int), override);
        MAKE_MOCK6(Padding, VideoFramePtr(VideoFramePtr, size_t, size_t, size_t, size_t, media::Color),
                   override);
        MAKE_MOCK1(EnsureHostData, bool(VideoFramePtr), override);
        MAKE_MOCK1(I4202BGR, VideoFramePtr(VideoFramePtr), override);
        MAKE_MOCK1(I4202RGB, VideoFramePtr(VideoFramePtr), override);
        MAKE_MOCK1(BGR2I420, VideoFramePtr(VideoFramePtr), override);
        MAKE_MOCK1(RGB2I420, VideoFramePtr(VideoFramePtr), override);
    };
}  // namespace

TEST_CASE("Workwear enrollment converts I420 before accessing inference models", "[workwear-enrollment]") {
    ScopedFrameMemoryPool memory_pool;
    using trompeloeil::_;
    EnrollmentFrameTransform transform;
    MockModelService models;
    ScopedServiceOverride<service::IVideoFrameTransform> transform_registration{transform};
    ScopedServiceOverride<service::IModelPathMapping> model_registration{models};
    auto input = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_I420);
    auto bgr   = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_BGR8);
    REQUIRE(VideoFrameValid(input));
    REQUIRE(VideoFrameValid(bgr));
    trompeloeil::sequence sequence;
    REQUIRE_CALL(transform, I4202BGR(input)).IN_SEQUENCE(sequence).RETURN(bgr);
    // Missing models stop this test before actual inference; conversion must happen first.
    REQUIRE_CALL(models, GetModelCfg("1001003", _, _)).IN_SEQUENCE(sequence).RETURN(false);
    REQUIRE_CALL(models, GetModelCfg("1001007", _, _)).IN_SEQUENCE(sequence).RETURN(false);
    service::BodyLibServiceImpl sut;
    CHECK(sut.ExtractBodyFeature(input).empty());
    CHECK(input->GetPixelFormat() == media::PixelFormat::PIXEL_I420);
}

TEST_CASE("Workwear enrollment stops on failed image conversion", "[workwear-enrollment]") {
    ScopedFrameMemoryPool memory_pool;
    using trompeloeil::_;
    EnrollmentFrameTransform transform;
    MockModelService models;
    ScopedServiceOverride<service::IVideoFrameTransform> transform_registration{transform};
    ScopedServiceOverride<service::IModelPathMapping> model_registration{models};
    auto input = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_I420);
    VideoFramePtr converted;
    SECTION("conversion returns no frame") {}
    SECTION("conversion returns an inactive frame") {
        converted = std::make_shared<media::VideoFrame>(0, 0, media::PixelFormat::PIXEL_BGR8);
    }
    SECTION("conversion leaves I420 unchanged") {
        converted = input;
    }
    REQUIRE_CALL(transform, I4202BGR(input)).RETURN(converted);
    FORBID_CALL(models, GetModelCfg(_, _, _));
    service::BodyLibServiceImpl sut;
    CHECK(sut.ExtractBodyFeature(input).empty());
}

TEST_CASE("Workwear enrollment keeps packed color frames", "[workwear-enrollment]") {
    ScopedFrameMemoryPool memory_pool;
    using trompeloeil::_;
    EnrollmentFrameTransform transform;
    MockModelService models;
    ScopedServiceOverride<service::IVideoFrameTransform> transform_registration{transform};
    ScopedServiceOverride<service::IModelPathMapping> model_registration{models};
    const auto format = GENERATE(media::PixelFormat::PIXEL_BGR8, media::PixelFormat::PIXEL_RGB8);
    auto input        = std::make_shared<media::VideoFrame>(4, 4, format);
    FORBID_CALL(transform, I4202BGR(_));
    REQUIRE_CALL(models, GetModelCfg("1001003", _, _)).RETURN(false);
    REQUIRE_CALL(models, GetModelCfg("1001007", _, _)).RETURN(false);
    service::BodyLibServiceImpl sut;
    CHECK(sut.ExtractBodyFeature(input).empty());
}

TEST_CASE("Recognizer propagates extraction failures to its single-image caller", "[workwear-enrollment]") {
    ScopedFrameMemoryPool memory_pool;
    AiRecognizerUnify recognizer("", "");
    auto input = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_BGR8);
    std::vector<AiDetectRstEl> targets(1);
    targets[0].box = {0, 0, 4, 4};
    REQUIRE(VideoFrameValid(input));
    CHECK(recognizer.Extract(input, targets, true) == util::ErrorEnum::NotInit);
    CHECK(targets[0].feature.feature.empty());
}

TEST_CASE("Recognizer rejects missing images without dereferencing them", "[workwear-enrollment]") {
    ScopedFrameMemoryPool memory_pool;
    AiRecognizerUnify recognizer("", "");
    std::vector<AiDetectRstEl> targets(1);
    VideoFramePtr input;
    SECTION("null image") {}
    SECTION("inactive image") {
        input = std::make_shared<media::VideoFrame>(0, 0, media::PixelFormat::PIXEL_BGR8);
    }
    CHECK(recognizer.Extract(input, targets, true) == util::ErrorEnum::FrameDataInvalid);
}
}  // namespace cosmo::test
