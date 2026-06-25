#include "catch_amalgamated.hpp"
/*
 * test_api_handler_livestream.cc - MessageLiveStreamHandler unit tests
 *
 * Tests for live stream request, keepalive, and stop.
 */
#include "api/MessageLiveStreamHandler.h"
#include "mock/MockLiveStreamService.h"
#include "mock/MockServiceRegistry.h"
#include "util/ErrorCode.h"

using namespace cosmo;

TEST_CASE("MessageLiveStreamHandler: Request stream success", "[LiveStreamHandler]") {
    test::MockServiceRegistry mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerCreate("ch1", "alg1", trompeloeil::_))
        .SIDE_EFFECT(_3.port = 8080)
        .SIDE_EFFECT(_3.keepAliveInterval = 10)
        .SIDE_EFFECT(_3.keepAliveUrl = "streamkeepalive")
        .SIDE_EFFECT(_3.flvUrl = "/live/ch1_alg1.flv")
        .SIDE_EFFECT(_3.hlsUrl = "/live/ch1_alg1.m3u8")
        .SIDE_EFFECT(_3.protocol = "webrtc")
        .RETURN(util::ErrorEnum::Success);

    LiveStream::MsgRequestLiveStreamRecv req{};
    req.channelId   = "ch1";
    req.algorithmId = "alg1";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
    REQUIRE(rsp.resData.stream.keepAliveInterval == 10);
    REQUIRE(rsp.resData.stream.keepAliveUrl == "streamkeepalive");
    // Stream name should be channelId_algorithmId
    REQUIRE(rsp.resData.stream.flvUrl.find("ch1_alg1") != std::string::npos);
    REQUIRE(rsp.resData.stream.hlsUrl.find("ch1_alg1") != std::string::npos);
}

TEST_CASE("MessageLiveStreamHandler: Request stream without algorithm", "[LiveStreamHandler]") {
    test::MockServiceRegistry mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerCreate("ch2", "", trompeloeil::_))
        .SIDE_EFFECT(_3.flvUrl = "/live/ch2.flv")
        .RETURN(util::ErrorEnum::Success);

    LiveStream::MsgRequestLiveStreamRecv req{};
    req.channelId   = "ch2";
    req.algorithmId = "";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
    // Stream name should be just channelId
    REQUIRE(rsp.resData.stream.flvUrl.find("ch2") != std::string::npos);
}

TEST_CASE("MessageLiveStreamHandler: Request stream failure", "[LiveStreamHandler]") {
    test::MockServiceRegistry mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerCreate("ch3", "", trompeloeil::_))
        .RETURN(util::ErrorEnum::ParameterException);

    LiveStream::MsgRequestLiveStreamRecv req{};
    req.channelId   = "ch3";
    req.algorithmId = "";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::ParameterException);
    // On failure, URLs should not be populated
    REQUIRE(rsp.resData.stream.flvUrl.empty());
}

TEST_CASE("MessageLiveStreamHandler: KeepAlive success", "[LiveStreamHandler]") {
    test::MockServiceRegistry mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerHeartBeat("ch1", "alg1")).RETURN(util::ErrorEnum::Success);

    LiveStream::MsgStreamKeepAliveRecv req{};
    req.channelId   = "ch1";
    req.algorithmId = "alg1";
    std::error_condition errc;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageLiveStreamHandler: KeepAlive failure", "[LiveStreamHandler]") {
    test::MockServiceRegistry mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerHeartBeat("bad_ch", ""))
        .RETURN(util::ErrorEnum::ParameterException);

    LiveStream::MsgStreamKeepAliveRecv req{};
    req.channelId   = "bad_ch";
    req.algorithmId = "";
    std::error_condition errc;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::ParameterException);
}

TEST_CASE("MessageLiveStreamHandler: Stop stream", "[LiveStreamHandler]") {
    test::MockServiceRegistry mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerDelete("ch1", "alg1")).RETURN(true);

    LiveStream::MsgStreamStopRecv req{};
    req.channelId   = "ch1";
    req.algorithmId = "alg1";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    // Stop always succeeds (void-like)
    REQUIRE(true);
}
