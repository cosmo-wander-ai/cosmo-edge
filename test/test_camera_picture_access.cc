#include <chrono>
#include <filesystem>
#include <fstream>

#include "api/ApiRouter.h"
#include "catch_amalgamated.hpp"
#include "nlohmann/json.hpp"
#include "support/ApiRouterTestDependencies.h"
#include "support/ScopedFrameMemoryPool.h"

using namespace cosmo;

TEST_CASE("Camera snapshots return image data only to authenticated HTTP clients",
          "[ApiRouter][camera-picture]") {
    namespace fs    = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("cosmo-camera-picture-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } cleanup{root};
    test::ScopedFrameMemoryPool pool;
    test::ApiRouterTestDependencies mocks;
    ALLOW_CALL(mocks.authSvc, IsValidToken("valid-token")).RETURN(true);
    ALLOW_CALL(mocks.authSvc, IsValidToken("invalid-token")).RETURN(false);
    ALLOW_CALL(mocks.authSvc, IsValidToken("")).RETURN(false);
    ALLOW_CALL(mocks.cameraSvc, GetWebLocalPath(trompeloeil::_)).LR_RETURN(root.string());
    ALLOW_CALL(mocks.cameraSvc, GetWebAccessPath(trompeloeil::_)).RETURN("/web/2026/11/19");

    const auto body = nlohmann::json{{"videoChannelId", "camera1"}}.dump();
    RequestDispatchContext request;
    request.uri       = "/gtw/cwai/Camera/GetPicture";
    request.transport = RequestTransport::kHttp;
    RequestDispatchResponse response;
    ApiRouter http(MessageFromType::MessageFromHttp);

    SECTION("invalid credentials do not capture") {
        FORBID_CALL(mocks.cameraSvc, CaptureImage(trompeloeil::_, trompeloeil::_));
        CHECK_FALSE(http.DispatchRequestResponse(request, body, response));
        request.credential = "invalid-token";
        CHECK_FALSE(http.DispatchRequestResponse(request, body, response));
    }
    SECTION("live capture preserves URL and supplies image bytes") {
        const auto frame = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_I420);
        const std::vector<uint8_t> jpeg{'i', 'm', 'a', 'g', 'e'};
        ALLOW_CALL(mocks.cameraSvc, CaptureImage("camera1", trompeloeil::_)).LR_RETURN(frame);
        ALLOW_CALL(mocks.cameraSvc, EncodeJpeg(trompeloeil::_)).LR_RETURN(jpeg);
        request.credential = "valid-token";
        REQUIRE(http.DispatchRequestResponse(request, body, response));
        const auto result = nlohmann::json::parse(response.body);
        REQUIRE(result["resCode"] == 1);
        CHECK(result["resData"]["url"].get<std::string>().find("/web/2026/11/19/") == 0);
        CHECK(result["resData"]["dataUrl"] == "data:image/jpeg;base64,aW1hZ2U=");
    }
    SECTION("cached capture returns data without following symlinks") {
        ALLOW_CALL(mocks.cameraSvc, CaptureImage("camera1", trompeloeil::_)).RETURN(nullptr);
        std::ofstream(root / "camera1.jpg", std::ios::binary) << "image";
        request.credential = "valid-token";
        REQUIRE(http.DispatchRequestResponse(request, body, response));
        auto result = nlohmann::json::parse(response.body);
        CHECK(result["resData"]["dataUrl"] == "data:image/jpeg;base64,aW1hZ2U=");
        fs::remove(root / "camera1.jpg");
        std::ofstream(root / "elsewhere.jpg", std::ios::binary) << "private";
        fs::create_symlink(root / "elsewhere.jpg", root / "camera1.jpg");
        REQUIRE(http.DispatchRequestResponse(request, body, response));
        result = nlohmann::json::parse(response.body);
        CHECK_FALSE(result["resData"].contains("dataUrl"));
    }
    SECTION("MQTT retains the URL-only response") {
        const auto frame = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_I420);
        const std::vector<uint8_t> jpeg{'i', 'm', 'a', 'g', 'e'};
        ALLOW_CALL(mocks.cameraSvc, CaptureImage("camera1", trompeloeil::_)).LR_RETURN(frame);
        ALLOW_CALL(mocks.cameraSvc, EncodeJpeg(trompeloeil::_)).LR_RETURN(jpeg);
        ApiRouter mqtt(MessageFromType::MessageFromMqtt);
        request.transport = RequestTransport::kMqtt;
        REQUIRE(mqtt.DispatchRequestResponse(request, body, response));
        const auto result = nlohmann::json::parse(response.body);
        CHECK(result["resData"].contains("url"));
        CHECK_FALSE(result["resData"].contains("dataUrl"));
    }
}
