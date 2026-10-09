#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <thread>

#include "catch_amalgamated.hpp"
#include "service/onvif/dto/OnvifDto.h"
#include "service/onvif/impl/OnvifProtocol.h"
#include "service/onvif/impl/OnvifServiceImpl.h"
#include "util/PathUtil.h"
#include "util/UuidUtil.h"

using namespace cosmo::service::onvif;
TEST_CASE("ONVIF discovery binds malformed and IPv6 advertisements to the IPv4 sender", "[onvif-discovery]") {
    const auto endpoints = DiscoveryEndpoints("ice http://[2001:db8::10]/onvif/device_service", "192.0.2.10");
    REQUIRE(endpoints == std::vector<std::string>{"http://192.0.2.10/onvif/device_service"});
}

TEST_CASE("ONVIF discovery preserves service paths without following advertised hosts", "[onvif-discovery]") {
    REQUIRE(DiscoveryEndpoints("https://camera.invalid:8443/custom/onvif", "192.0.2.10") ==
            std::vector<std::string>{"https://192.0.2.10:8443/custom/onvif"});
    REQUIRE(DiscoveryEndpoints("ice", "192.0.2.10") ==
            std::vector<std::string>{"http://192.0.2.10/onvif/device_service"});
    REQUIRE(DiscoveryEndpoints("http://192.0.2.10/onvif/device_service", "192.0.2.10") ==
            std::vector<std::string>{"http://192.0.2.10/onvif/device_service"});
}

namespace {
std::string Envelope(const std::string& body) {
    return "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\"><e:Body>" + body +
           "</e:Body></e:Envelope>";
}
struct ConfigRoot {
    std::string previous    = cosmo::path::GetBaseDir();
    std::string previousApp = cosmo::path::GetAppBaseDir();
    std::string root =
        (std::filesystem::temp_directory_path() / ("cosmo-onvif-" + cosmo::util::GenerateUUID())).string();
    ConfigRoot() {
        cosmo::path::OverrideRootPathForTest(root, root);
    }
    ~ConfigRoot() {
        cosmo::path::OverrideRootPathForTest(previous, previousApp);
        std::filesystem::remove_all(root);
    }
};
// A SOAP camera that rejects empty POST probes, as WSSE-only cameras do.
class SoapCamera {
public:
    explicit SoapCamera(bool digest = false, std::string profiles = "", std::string encoder = "",
                        bool streamUri = true)
        : requireDigest(digest),
          profiles_(std::move(profiles)),
          encoder_(std::move(encoder)),
          stream_uri_(streamUri) {
        listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
            listen(listener, 8))
            throw std::runtime_error("test listener failed");
        socklen_t size = sizeof(address);
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size);
        endpoint = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/onvif/device_service";
        worker   = std::thread([this] { Serve(); });
    }
    ~SoapCamera() {
        stopping = true;
        worker.join();
        close(listener);
    }
    std::string endpoint;
    std::atomic<int> emptyPosts{0};
    std::atomic<int> authenticatedPosts{0};
    std::atomic<int> streamRequests{0};

private:
    void Serve() {
        while (!stopping) {
            pollfd p{listener, POLLIN, 0};
            if (poll(&p, 1, 50) <= 0)
                continue;
            const int client = accept(listener, nullptr, nullptr);
            if (client < 0)
                continue;
            timeval timeout{1, 0};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            std::string request;
            char buffer[4096];
            size_t bodyStart  = std::string::npos;
            size_t bodyLength = 0;
            while (request.size() < 65536) {
                auto count = recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    break;
                request.append(buffer, count);
                bodyStart = request.find("\r\n\r\n");
                if (bodyStart == std::string::npos)
                    continue;
                auto length = request.find("Content-Length:");
                bodyLength  = length == std::string::npos ? 0 : std::stoul(request.substr(length + 15));
                bodyStart += 4;
                if (request.size() >= bodyStart + bodyLength)
                    break;
            }
            std::string response;
            if (requireDigest && request.find("Authorization: Digest ") == std::string::npos) {
                response =
                    "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"camera\", "
                    "nonce=\"test-nonce\", qop=\"auth\", algorithm=MD5\r\nContent-Length: 0\r\nConnection: "
                    "close\r\n\r\n";
            } else if (!bodyLength || bodyStart == std::string::npos) {
                ++emptyPosts;
                response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            } else {
                if (requireDigest)
                    ++authenticatedPosts;
                auto body = request.substr(bodyStart, bodyLength);
                std::string operation, inner;
                const std::string device = "http://www.onvif.org/ver10/device/wsdl";
                const std::string media  = "http://www.onvif.org/ver10/media/wsdl";
                std::string ns           = device;
                if (body.find("<t:GetSystemDateAndTime") != std::string::npos) {
                    operation = "GetSystemDateAndTime";
                } else if (body.find("<t:GetServices") != std::string::npos) {
                    operation = "GetServices";
                    inner     = "<Service><Namespace>" + media + "</Namespace><XAddr>" + endpoint +
                            "</XAddr></Service>";
                } else if (body.find("<t:GetProfiles") != std::string::npos) {
                    operation = "GetProfiles";
                    ns        = media;
                    inner =
                        profiles_.empty()
                            ? "<Profiles "
                              "token=\"main\"><Name>Main</Name><VideoEncoderConfiguration><Encoding>H264</"
                              "Encoding><Resolution><Width>1920</Width><Height>1080</Height></Resolution>"
                              "</VideoEncoderConfiguration></Profiles>"
                            : profiles_;
                } else if (body.find("<t:GetVideoEncoderConfiguration") != std::string::npos) {
                    operation = "GetVideoEncoderConfiguration";
                    ns        = media;
                    inner     = encoder_;
                } else if (body.find("<t:GetStreamUri") != std::string::npos) {
                    ++streamRequests;
                    operation = "GetStreamUri";
                    ns        = media;
                    inner = stream_uri_ ? "<MediaUri><Uri>rtsp://127.0.0.1:554/live</Uri></MediaUri>" : "";
                }
                const auto xml = Envelope("<t:" + operation + "Response xmlns:t=\"" + ns + "\">" + inner +
                                          "</t:" + operation + "Response>");
                response       = "HTTP/1.1 200 OK\r\nContent-Type: application/soap+xml\r\nContent-Length: " +
                           std::to_string(xml.size()) + "\r\nConnection: close\r\n\r\n" + xml;
            }
            size_t sent = 0;
            while (sent < response.size()) {
                auto count = send(client, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
                if (count <= 0)
                    break;
                sent += count;
            }
            close(client);
        }
    }
    int listener{-1};
    const bool requireDigest;
    const std::string profiles_;
    const std::string encoder_;
    const bool stream_uri_;
    std::atomic<bool> stopping{false};
    std::thread worker;
};
}  // namespace

TEST_CASE("ONVIF sends SOAP before attempting HTTP Digest negotiation", "[onvif][onvif-http]") {
    SoapCamera camera;
    Config config;
    config.endpoint     = camera.endpoint;
    config.username     = "test-user";
    config.password     = "test-password";
    config.profileToken = "main";
    CHECK_NOTHROW(Client(config).Probe());
    CHECK_NOTHROW(Client(config).StreamUri());
    CHECK(camera.emptyPosts.load() == 0);
}

TEST_CASE("ONVIF answers an HTTP Digest challenge when the camera requires it", "[onvif][onvif-http]") {
    SoapCamera camera(true);
    Config config;
    config.endpoint     = camera.endpoint;
    config.username     = "test-user";
    config.password     = "test-password";
    config.profileToken = "main";
    CHECK_NOTHROW(Client(config).Probe());
    CHECK_NOTHROW(Client(config).StreamUri());
    CHECK(camera.authenticatedPosts.load() >= 4);
}

TEST_CASE("ONVIF accepts device endpoints but rejects embedded credentials", "[onvif]") {
    CHECK(NormalizeEndpoint("192.0.2.1:8080") == "http://192.0.2.1:8080/onvif/device_service");
    CHECK(NormalizeEndpoint("https://camera.example/device") == "https://camera.example/device");
    CHECK_THROWS(NormalizeEndpoint("file:///etc/passwd"));
    CHECK_THROWS(NormalizeEndpoint("http://user:secret@camera.example/onvif"));
    CHECK_THROWS(NormalizeEndpoint("http://camera.example/\r\n"));
}
TEST_CASE("ONVIF stream credentials are encoded exactly once", "[onvif]") {
    Config c;
    c.username     = "operator";
    c.password     = "a@b:#%";
    const auto uri = AuthenticatedUri("rtsp://device:discard@camera.example:554/live?x=1", c);
    CHECK(uri.find("discard") == std::string::npos);
    CHECK(uri.find("a%40b%3A%23%25") != std::string::npos);
    CHECK_THROWS(AuthenticatedUri("http://camera.example/live", c));
}
TEST_CASE("ONVIF parses Media and Media2 profiles with differing namespace prefixes", "[onvif]") {
    const auto v1 = Envelope(
        R"(<m:GetProfilesResponse xmlns:m="http://www.onvif.org/ver10/media/wsdl" xmlns:v="http://www.onvif.org/ver10/schema"><m:Profiles token="main&amp;one"><v:Name>Main</v:Name><v:VideoSourceConfiguration><v:SourceToken>sensor1</v:SourceToken></v:VideoSourceConfiguration><v:VideoEncoderConfiguration><v:Encoding>H264</v:Encoding><v:Resolution><v:Width>1920</v:Width><v:Height>1080</v:Height></v:Resolution><v:RateControl><v:FrameRateLimit>25</v:FrameRateLimit></v:RateControl></v:VideoEncoderConfiguration></m:Profiles></m:GetProfilesResponse>)");
    auto profiles = ParseProfiles(v1);
    REQUIRE(profiles.size() == 1);
    CHECK(profiles[0]["token"] == "main&one");
    CHECK(profiles[0]["width"] == 1920);
    CHECK(profiles[0]["videoSourceToken"] == "sensor1");
    const auto v2 = Envelope(
        R"(<GetProfilesResponse xmlns="http://www.onvif.org/ver20/media/wsdl"><Profiles token="low"><Name>Low</Name><Configurations><VideoEncoder><Encoding>H265</Encoding><Resolution><Width>640</Width><Height>360</Height></Resolution></VideoEncoder></Configurations></Profiles></GetProfilesResponse>)");
    CHECK(ParseProfiles(v2)[0]["codec"] == "H265");
    CHECK(ParseProfiles(Envelope("<GetProfilesResponse/>")).empty());
}
TEST_CASE("ONVIF rejects malformed and entity-bearing XML", "[onvif]") {
    CHECK_THROWS(Parse("<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>" + Envelope("<x/>")));
    CHECK_THROWS(Parse("<broken>"));
    CHECK_THROWS(Parse(std::string(1024 * 1024 + 1, 'x')));
    CHECK_THROWS(Parse("<Envelope/>"));
}
TEST_CASE("ONVIF retains video profiles missing encoder metadata", "[onvif][onvif-incomplete]") {
    const auto xml = Envelope(
        R"(<GetProfilesResponse><Profiles token="main"><Name>Main</Name><VideoSourceConfiguration><SourceToken>sensor</SourceToken></VideoSourceConfiguration></Profiles><Profiles token="audio"><AudioSourceConfiguration/></Profiles><Profiles><VideoSourceConfiguration/></Profiles></GetProfilesResponse>)");
    const auto profiles = ParseProfiles(xml);
    REQUIRE(profiles.size() == 1);
    CHECK(profiles[0]["token"] == "main");
    CHECK(profiles[0]["videoSourceToken"] == "sensor");
    CHECK(profiles[0]["codec"] == "");
    CHECK(profiles[0]["width"] == 0);
}
namespace {
const std::string kIncompleteProfile =
    "<Profiles token=\"main\"><Name>Main</Name><VideoSourceConfiguration>"
    "<SourceToken>sensor</SourceToken></VideoSourceConfiguration></Profiles>";
Config CameraConfig(const SoapCamera& camera) {
    Config config;
    config.endpoint = camera.endpoint;
    config.username = "test-user";
    config.password = "test-password";
    return config;
}
nlohmann::json HevcMetadata() {
    return {{"codec", "H265"}, {"width", 1920}, {"height", 1080}, {"fps", 25}};
}
}  // namespace

TEST_CASE("ONVIF fills missing HEVC metadata through authenticated RTSP", "[onvif][onvif-incomplete]") {
    SoapCamera camera(false, kIncompleteProfile);
    auto config                    = CameraConfig(camera);
    config.separateRtspCredentials = true;
    config.rtspUsername            = "stream-user";
    config.rtspPassword            = "stream@password";
    int calls                      = 0;
    const auto result              = Client(config, nullptr, [&](const auto& uri, auto deadline, auto*) {
                            ++calls;
                            CHECK(uri == "rtsp://stream-user:stream%40password@127.0.0.1:554/live");
                            const auto remaining = deadline - std::chrono::steady_clock::now();
                            CHECK(remaining > std::chrono::seconds(0));
                            CHECK(remaining <= std::chrono::seconds(5));
                            auto metadata   = HevcMetadata();
                            metadata["url"] = uri;
                            return metadata;
                        }).Probe();
    REQUIRE(result["profiles"].size() == 1);
    CHECK(result["profiles"][0]["codec"] == "H265");
    CHECK(result["profiles"][0]["width"] == 1920);
    CHECK(result["profiles"][0]["videoSourceToken"] == "sensor");
    CHECK(result.dump().find("password") == std::string::npos);
    CHECK_FALSE(result["profiles"][0].contains("encoderToken"));
    CHECK(calls == 1);
    CHECK(camera.streamRequests.load() == 1);
}

TEST_CASE("ONVIF complete profiles do not open RTSP during addition", "[onvif][onvif-incomplete]") {
    SoapCamera camera;
    int calls = 0;
    CHECK_NOTHROW(Client(CameraConfig(camera), nullptr, [&](const auto&, auto, auto*) {
                      ++calls;
                      return HevcMetadata();
                  }).Probe());
    CHECK(calls == 0);
    CHECK(camera.streamRequests.load() == 0);
}

TEST_CASE("ONVIF supplements only an explicitly linked encoder configuration", "[onvif][onvif-incomplete]") {
    SoapCamera camera(false,
                      "<Profiles token=\"main\"><VideoEncoderConfiguration token=\"encoder\"/></Profiles>",
                      "<Configuration token=\"encoder\"><Encoding>H265</Encoding><Resolution>"
                      "<Width>1920</Width><Height>1080</Height></Resolution></Configuration>");
    int calls         = 0;
    const auto result = Client(CameraConfig(camera), nullptr, [&](const auto&, auto, auto*) {
                            ++calls;
                            return HevcMetadata();
                        }).Probe();
    CHECK(result["profiles"][0]["codec"] == "H265");
    CHECK(calls == 0);
    CHECK(camera.streamRequests.load() == 0);
}

TEST_CASE("ONVIF unsupported optional configuration query falls back to RTSP", "[onvif][onvif-incomplete]") {
    SoapCamera camera(false,
                      "<Profiles token=\"main\"><VideoEncoderConfiguration token=\"encoder\"/></Profiles>");
    const auto result = Client(CameraConfig(camera), nullptr, [](const auto&, auto, auto*) {
                            return HevcMetadata();
                        }).Probe();
    CHECK(result["profiles"][0]["codec"] == "H265");
    CHECK(camera.streamRequests.load() == 1);
}

TEST_CASE("ONVIF incomplete profiles report URI and media failures distinctly", "[onvif][onvif-incomplete]") {
    SECTION("no URI") {
        SoapCamera camera(false, kIncompleteProfile, "", false);
        CHECK_THROWS_WITH(Client(CameraConfig(camera)).Probe(), "stream_uri_failed");
    }
    SECTION("media unavailable") {
        SoapCamera camera(false, kIncompleteProfile);
        CHECK_THROWS_WITH(Client(CameraConfig(camera), nullptr,
                                 [](const auto&, auto, auto*) -> nlohmann::json {
                                     throw std::runtime_error("stream_probe_failed");
                                 })
                              .Probe(),
                          "stream_probe_failed");
    }
    SECTION("authentication failure stops further attempts") {
        SoapCamera camera(false, kIncompleteProfile + kIncompleteProfile);
        CHECK_THROWS_WITH(Client(CameraConfig(camera), nullptr,
                                 [](const auto&, auto, auto*) -> nlohmann::json {
                                     throw std::runtime_error("rtsp_unauthorized");
                                 })
                              .Probe(),
                          "rtsp_unauthorized");
        CHECK(camera.streamRequests.load() == 1);
    }
}

TEST_CASE("ONVIF one failed profile does not hide another usable stream", "[onvif][onvif-incomplete]") {
    SoapCamera camera(false, kIncompleteProfile + kIncompleteProfile);
    int calls         = 0;
    const auto result = Client(CameraConfig(camera), nullptr, [&](const auto&, auto, auto*) {
                            if (++calls == 1)
                                throw std::runtime_error("stream_probe_failed");
                            return HevcMetadata();
                        }).Probe();
    CHECK(result["profiles"][1]["codec"] == "H265");
    CHECK(calls == 2);
}

TEST_CASE("ONVIF limits fallback work and does not invent video dimensions", "[onvif][onvif-incomplete]") {
    std::string profiles;
    for (int i = 0; i < 10; ++i)
        profiles += kIncompleteProfile;
    SoapCamera camera(false, profiles);
    int calls = 0;
    CHECK_THROWS_WITH(Client(CameraConfig(camera), nullptr,
                             [&](const auto&, auto, auto*) {
                                 ++calls;
                                 auto metadata     = HevcMetadata();
                                 metadata["width"] = 0;
                                 return metadata;
                             })
                          .Probe(),
                      "stream_probe_failed");
    CHECK(calls == 4);
}
TEST_CASE("ONVIF request serialization does not expose submitted passwords", "[onvif]") {
    cosmo::camera::MsgOnvifSaveRecv request;
    nlohmann::json{{"password", "private-token"}, {"rtspPassword", "another-token"}}.get_to(request);
    const nlohmann::json output = request;
    CHECK(output.dump().find("private-token") == std::string::npos);
    CHECK(output.dump().find("another-token") == std::string::npos);
}
TEST_CASE("ONVIF persisted credentials survive restart and edits preserve omitted secrets", "[onvif]") {
    ConfigRoot paths;
    cosmo::service::OnvifServiceImpl service;
    service.Init();
    const auto source = service.Save({{"endpoint", "192.0.2.1"},
                                      {"username", "operator"},
                                      {"password", "private-camera-password"},
                                      {"profileToken", "main"}});
    auto metadata     = service.Describe(source);
    CHECK(metadata["hasPassword"] == true);
    CHECK_FALSE(metadata.contains("password"));
    CHECK(service.Revision(source) == 1);
    service.Save({{"profileToken", "sub"}}, source);
    CHECK(service.Revision(source) == 2);
    CHECK(service.Describe(source)["hasPassword"] == true);
    std::ifstream file(cosmo::path::GetCfgPath("onvif") + "/sources.json");
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    CHECK(bytes.find("private-camera-password") == std::string::npos);
    cosmo::service::OnvifServiceImpl restarted;
    restarted.Init();
    CHECK(restarted.Describe(source)["profileToken"] == "sub");
    CHECK(restarted.Describe(source)["hasPassword"] == true);
    CHECK_THROWS(
        restarted.Save({{"endpoint", "192.0.2.1"}, {"username", "operator"}, {"profileToken", "sub"}}));
    restarted.Remove(source);
    CHECK(restarted.Revision(source) == 0);
    cosmo::service::OnvifServiceImpl removed;
    removed.Init();
    CHECK(removed.Revision(source) == 0);
}
