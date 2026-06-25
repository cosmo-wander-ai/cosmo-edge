#include "catch_amalgamated.hpp"
/*
 * test_video_frame_service_impl.cc — VideoFrameServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: VideoFrameServiceImpl wraps VPU image processing (bmcv).
 * Construction creates VideoFrameProc which requires bmlib device init.
 * All tests tagged [.device].
 */
#include "mock/MockServiceRegistry.h"
#include "service/media/impl/VideoFrameServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("VideoFrameServiceImpl: construction and destruction", "[VideoFrameService][.device]") {
    REQUIRE_NOTHROW([]() { VideoFrameServiceImpl sut; }());
}

TEST_CASE("VideoFrameServiceImpl: EncodeJpeg with null frame returns empty", "[VideoFrameService][.device]") {
    VideoFrameServiceImpl sut;
    VideoFramePtr nullFrame;
    auto data = sut.EncodeJpeg(nullFrame);
    REQUIRE(data.empty());
}

TEST_CASE("VideoFrameServiceImpl: DecodeJpeg with empty data returns null", "[VideoFrameService][.device]") {
    VideoFrameServiceImpl sut;
    std::vector<uint8_t> emptyData;
    auto frame = sut.DecodeJpeg(emptyData);
    REQUIRE(frame == nullptr);
}

TEST_CASE("VideoFrameServiceImpl: EnsureHostData with null frame", "[VideoFrameService][.device]") {
    VideoFrameServiceImpl sut;
    VideoFramePtr nullFrame;
    REQUIRE(sut.EnsureHostData(nullFrame) == false);
}

TEST_CASE("VideoFrameServiceImpl: Crop with null frame", "[VideoFrameService][.device]") {
    VideoFrameServiceImpl sut;
    VideoFramePtr nullFrame;
    cosmo::util::Box roi{0, 0, 100, 100};
    auto result = sut.Crop(nullFrame, roi);
    REQUIRE(result == nullptr);
}
