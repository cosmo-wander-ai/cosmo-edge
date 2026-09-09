#include "catch_amalgamated.hpp"
/*
 * test_api_handler_livestream.cc - MessageLiveStreamHandler unit tests
 *
 * Tests for live stream request, keepalive, and stop.
 */
#include "api/ApiRouterInternal.h"
#include "api/MessageLiveStreamHandler.h"
#include "mock/MockLiveStreamService.h"
#include "util/ErrorCode.h"

using namespace cosmo;

namespace {
struct LiveStreamHandlerMocks {
    test::MockLiveStreamService liveStreamSvc;
};
}  // namespace

TEST_CASE("MessageLiveStreamHandler: Request stream success", "[LiveStreamHandler]") {
    LiveStreamHandlerMocks mocks;
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
    LiveStreamHandlerMocks mocks;
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
    LiveStreamHandlerMocks mocks;
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
    LiveStreamHandlerMocks mocks;
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
    LiveStreamHandlerMocks mocks;
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
    LiveStreamHandlerMocks mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerDelete("ch1", "alg1")).RETURN(true);

    LiveStream::MsgStreamStopRecv req{};
    req.channelId   = "ch1";
    req.algorithmId = "alg1";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    (void)rsp;
    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageLiveStreamHandler: Stop stream propagates failure", "[LiveStreamHandler]") {
    LiveStreamHandlerMocks mocks;
    MessageLiveStreamHandler handler(mocks.liveStreamSvc);

    ALLOW_CALL(mocks.liveStreamSvc, ViewerDelete("ch1", "alg1")).RETURN(false);

    LiveStream::MsgStreamStopRecv req{};
    req.channelId             = "ch1";
    req.algorithmId           = "alg1";
    std::error_condition errc = util::ErrorEnum::Success;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Failed);
}

TEST_CASE("ApiRouter: generic live stream exception is never serialized as Success", "[LiveStreamHandler]") {
    std::error_condition errc = util::ErrorEnum::Success;
    const auto payload        = nlohmann::json::parse(detail::ErroResult("Input/output error", errc));

    REQUIRE(errc == util::ErrorEnum::InternalError);
    REQUIRE(payload.at("resCode") == kServerRspFailed);
    REQUIRE(payload.at("resMsg").at(0).at("msgCode") ==
            std::to_string(static_cast<uint32_t>(util::ErrorEnum::InternalError)));
    REQUIRE(payload.at("resMsg").at(0).at("messageKey") != "api.error.Success");
    REQUIRE(payload.at("resMsg").at(0).at("msgText") == "Input/output error");
}
