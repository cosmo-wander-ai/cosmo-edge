#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <condition_variable>
#include <filesystem>
#include <mutex>

#include "catch_amalgamated.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/gb28181/impl/Gb28181ManagementImpl.h"
#include "service/gb28181/impl/GbSipProtocol.h"
#include "service/network/IHttpClient.h"
#include "util/PathUtil.h"

namespace {
using Json                      = nlohmann::json;
namespace gb                    = cosmo::service::gb;
constexpr const char* deviceId  = "34020000001110000001";
constexpr const char* channelId = "34020000001320000002";
struct Root {
    std::string previous = cosmo::path::GetBaseDir(), previousApp = cosmo::path::GetAppBaseDir();
    std::string root =
        (std::filesystem::temp_directory_path() / ("cosmo-gb-runtime-" + gb::RandomHex())).string();
    Root() {
        cosmo::path::OverrideRootPathForTest(root, root);
    }
    ~Root() {
        cosmo::path::OverrideRootPathForTest(previous, previousApp);
        std::filesystem::remove_all(root);
    }
};
class Media final : public cosmo::service::IHttpClient {
public:
    std::mutex mtx;
    std::condition_variable changed;
    std::vector<Json> calls;
    cosmo::service::HttpResponse Get(const std::string&, long, long,
                                     const std::vector<std::pair<std::string, std::string>>&) override {
        return {200, R"({"code":0,"streams":[]})"};
    }
    cosmo::service::HttpResponse Post(const std::string&, const std::string& body, const std::string&, long,
                                      long,
                                      const std::vector<std::pair<std::string, std::string>>&) override {
        std::lock_guard<std::mutex> lock(mtx);
        calls.push_back(Json::parse(body));
        changed.notify_all();
        return {200, R"({"code":0,"port":19001,"is_tcp":true,"managed":true})"};
    }
};
struct Registration {
    explicit Registration(Media& media) {
        cosmo::service::ServiceRegistry::Instance().Set<cosmo::service::IHttpClient>(&media);
    }
    ~Registration() {
        cosmo::service::ServiceRegistry::Instance().Set<cosmo::service::IHttpClient>(nullptr);
    }
};
int FreePort() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t length = sizeof(address);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const int port = ntohs(address.sin_port);
    close(fd);
    return port;
}
class Camera {
public:
    int fd{-1};
    std::string input;
    explicit Camera(int port) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = htons(port);
        REQUIRE(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    }
    ~Camera() {
        if (fd >= 0)
            close(fd);
    }
    void Send(const std::string& bytes) {
        REQUIRE(send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size()));
    }
    gb::Message Read() {
        gb::Message message;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!gb::Pop(input, message)) {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            pollfd descriptor{fd, POLLIN, 0};
            if (poll(&descriptor, 1, 100) <= 0)
                continue;
            char data[16384];
            const auto count = recv(fd, data, sizeof(data), 0);
            REQUIRE(count > 0);
            input.append(data, count);
        }
        return message;
    }
};
std::string Packet(const std::string& method, const std::string& body = "", const std::string& extra = "",
                   const std::string& id = deviceId) {
    return method +
           " sip:34020000002000000001@3402000000 SIP/2.0\r\nVia: SIP/2.0/TCP "
           "127.0.0.1:15060;branch=z9hG4bKfixture\r\nFrom: <sip:" +
           id + "@3402000000>;tag=camera\r\nTo: <sip:" + id + "@3402000000>\r\nCall-ID: fixture\r\nCSeq: 1 " +
           method + "\r\nContact: <sip:" + id + "@127.0.0.1:15060>\r\n" + extra +
           "Content-Type: Application/MANSCDP+xml\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
}
std::string Authorization(const gb::Message& challenge, bool correct = true) {
    const auto header = challenge.Header("www-authenticate");
    const auto start  = header.find("nonce=\"");
    REQUIRE(start != std::string::npos);
    const auto nonce      = header.substr(start + 7, header.find('"', start + 7) - start - 7);
    const std::string uri = "sip:34020000002000000001@3402000000";
    const auto digest     = gb::Md5(
        gb::Md5(std::string("auth-account:3402000000:") + (correct ? "fixture-password" : "incorrect")) +
        ":" + nonce + ":00000001:fixture:auth:" + gb::Md5("REGISTER:" + uri));
    return "Authorization: Digest username=\"auth-account\",realm=\"3402000000\",nonce=\"" + nonce +
           "\",uri=\"" + uri + "\",response=\"" + digest + "\",qop=auth,nc=00000001,cnonce=\"fixture\"\r\n";
}
std::string Catalog(const std::string& sn, const std::string& channel, int total = 1) {
    return "<Response><CmdType>Catalog</CmdType><SN>" + sn + "</SN><DeviceID>" + deviceId +
           "</DeviceID><SumNum>" + std::to_string(total) + "</SumNum><DeviceList Num=\"1\"><Item><DeviceID>" +
           channel +
           "</DeviceID><Name>Fixture channel</Name><Status>ON</Status></Item></DeviceList></Response>";
}
}  // namespace
TEST_CASE("Managed GB challenges registration then catalogs and invites a distinct video channel",
          "[gb28181][gb-managed]") {
    Root root;
    Media media;
    Registration registration(media);
    cosmo::service::Gb28181ManagementImpl service;
    service.Init();
    const int port = FreePort();
    service.Execute({{"action", "savePlatform"}, {"enabled", true}, {"sipPort", port}});
    CHECK_THROWS(service.Execute({{"action", "saveDevice"}, {"id", deviceId}}));
    service.Execute({{"action", "saveDevice"},
                     {"id", deviceId},
                     {"username", "auth-account"},
                     {"password", "fixture-password"}});
    const auto publicState = service.Execute({{"action", "list"}});
    CHECK(publicState.dump().find("fixture-password") == std::string::npos);
    CHECK(publicState.dump().find("ha1") == std::string::npos);
    CHECK_THROWS(service.Execute({{"action", "savePlatform"}, {"realm", "4402000000"}}));
    Camera camera(port);
    camera.Send(Packet("REGISTER"));
    auto challenge = camera.Read();
    REQUIRE(challenge.status == 401);
    camera.Send(Packet("REGISTER", "", Authorization(challenge, false)));
    challenge = camera.Read();
    REQUIRE(challenge.status == 401);
    const auto auth = Authorization(challenge);
    camera.Send(Packet("REGISTER", "", auth));
    REQUIRE(camera.Read().status == 200);
    auto query = camera.Read();
    REQUIRE(query.method == "MESSAGE");
    REQUIRE(query.body.find("<CmdType>Catalog</CmdType>") != std::string::npos);
    const auto start = query.body.find("<SN>");
    const auto sn    = query.body.substr(start + 4, query.body.find("</SN>") - start - 4);
    // A consumed nonce cannot authenticate again.
    camera.Send(Packet("REGISTER", "", auth));
    REQUIRE(camera.Read().status == 401);
    camera.Send(Packet("MESSAGE", Catalog(sn, "34020000001320000001", 2)));
    REQUIRE(camera.Read().status == 200);
    CHECK_THROWS(service.Execute(
        {{"action", "validateChannel"}, {"deviceId", deviceId}, {"channelId", "34020000001320000001"}}));
    camera.Send(Packet("MESSAGE", Catalog(sn, channelId, 2)));
    REQUIRE(camera.Read().status == 200);
    CHECK(service.Execute(
              {{"action", "validateChannel"}, {"deviceId", deviceId}, {"channelId", channelId}})["name"] ==
          "Fixture channel");
    service.Ensure(channelId);
    auto invite = camera.Read();
    REQUIRE(invite.method == "INVITE");
    CHECK(gb::User(invite.uri) == channelId);
    CHECK(gb::User(invite.Header("to")) == channelId);
    CHECK(invite.body.find("19001 TCP/RTP/AVP 96") != std::string::npos);
    CHECK(invite.body.find("a=setup:passive") != std::string::npos);
    auto answer = gb::Response(invite, 200, "Content-Type: application/sdp\r\n");
    const std::string sdp =
        "v=0\r\nm=video 19002 TCP/RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\na=setup:active\r\na=sendonly\r\n";
    answer.replace(answer.find("Content-Length: 0"), 17, "Content-Length: " + std::to_string(sdp.size()));
    answer += sdp;
    camera.Send(answer);
    auto ack = camera.Read();
    CHECK(ack.method == "ACK");
    CHECK(ack.Header("call-id") == invite.Header("call-id"));
    camera.Send(answer);
    CHECK(camera.Read().method == "ACK");
    service.Execute({{"action", "savePlatform"}, {"enabled", false}});
    std::unique_lock<std::mutex> lock(media.mtx);
    REQUIRE(media.changed.wait_for(lock, std::chrono::seconds(3), [&] { return media.calls.size() >= 2; }));
    REQUIRE(media.calls.size() >= 2);
    CHECK(media.calls.front()["id"] == channelId);
    CHECK(media.calls.back()["action"] == "release");
}
TEST_CASE("Managed GB rejects unlisted devices even in compatibility mode", "[gb28181][gb-managed]") {
    Root root;
    Media media;
    Registration registration(media);
    cosmo::service::Gb28181ManagementImpl service;
    service.Init();
    const int port = FreePort();
    service.Execute({{"action", "savePlatform"}, {"enabled", true}, {"sipPort", port}});
    service.Execute({{"action", "saveDevice"}, {"id", deviceId}, {"allowUnauthenticated", true}});
    Camera camera(port);
    camera.Send(Packet("REGISTER", "", "", "34020000001110000009"));
    CHECK(camera.Read().status == 403);
    camera.Send(Packet("REGISTER"));
    CHECK(camera.Read().status == 200);
    CHECK(camera.Read().method == "MESSAGE");
    Camera other(port);
    other.Send(Packet("REGISTER"));
    CHECK(other.Read().status == 403);
}
