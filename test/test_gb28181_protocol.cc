#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "service/gb28181/dto/Gb28181Dto.h"
#include "service/gb28181/impl/GbConfigStore.h"
#include "service/gb28181/impl/GbSipProtocol.h"

using namespace cosmo::service::gb;
namespace {
std::string Frame(const std::string& extra = "", const std::string& body = "") {
    return "REGISTER sip:34020000002000000001@3402000000 SIP/2.0\r\nVia: SIP/2.0/TCP "
           "192.0.2.1:5060;branch=z9hG4bKtest\r\nFrom: "
           "<sip:34020000001320000001@3402000000>;tag=camera\r\nTo: "
           "<sip:34020000001320000001@3402000000>\r\nCall-ID: fixture\r\nCSeq: 1 REGISTER\r\n" +
           extra + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
Message Request() {
    auto frame = Frame();
    Message request;
    REQUIRE(Pop(frame, request));
    return request;
}
std::string Auth(const Message& request, const std::string& nonce,
                 const std::string& username = "auth-account", bool qop = true) {
    const auto hash =
        Md5(Md5(username + ":3402000000:fixture-password") + ":" + nonce +
            (qop ? ":00000001:fixture-cnonce:auth:" : ":") + Md5(request.method + ":" + request.uri));
    return "Digest username=\"" + username + "\",realm=\"3402000000\",nonce=\"" + nonce + "\",uri=\"" +
           request.uri + "\",response=\"" + hash + "\"" +
           (qop ? ",qop=auth,nc=00000001,cnonce=\"fixture-cnonce\"" : "");
}
}  // namespace
TEST_CASE("GB TCP framing preserves split and coalesced messages", "[gb28181][gb-managed]") {
    auto whole = Frame("Contact: <sip:34020000001320000001@192.0.2.1:5060>\r\n", "body");
    std::string input;
    Message message;
    for (size_t i = 0; i + 1 < whole.size(); ++i) {
        input += whole[i];
        CHECK_FALSE(Pop(input, message));
    }
    input += whole.back();
    REQUIRE(Pop(input, message));
    CHECK(message.body == "body");
    CHECK(input.empty());
    input = "\r\n\r\n" + whole + whole;
    REQUIRE(Pop(input, message));
    REQUIRE(Pop(input, message));
    CHECK(input.empty());
}
TEST_CASE("GB rejects ambiguous, oversized and injected SIP frames", "[gb28181][gb-managed]") {
    Message message;
    auto frame = Frame("Content-Length: 0\r\n");
    CHECK_THROWS(Pop(frame, message));
    frame = Frame("l: 0\r\n");
    CHECK_THROWS(Pop(frame, message));
    frame = Frame("X-Fixture: ok\rbad\r\n");
    CHECK_THROWS(Pop(frame, message));
    frame = Frame(" Invalid: folded\r\n");
    CHECK_THROWS(Pop(frame, message));
    frame = std::string(16385, 'x');
    CHECK_THROWS(Pop(frame, message));
    frame = Frame();
    frame.replace(frame.find("Content-Length: 0"), 17, "Content-Length: 2000000");
    CHECK_THROWS(Pop(frame, message));
}
TEST_CASE("GB digest binds auth ID realm URI method nonce and password", "[gb28181][gb-managed]") {
    auto request   = Request();
    const auto ha1 = Md5("auth-account:3402000000:fixture-password");
    for (bool qop : {true, false}) {
        request.headers["authorization"] = Auth(request, "fixture-nonce", "auth-account", qop);
        CHECK(VerifyDigest(request, "auth-account", "3402000000", ha1, "fixture-nonce"));
        CHECK_FALSE(VerifyDigest(request, "auth-account", "3402000000", ha1, ""));
        CHECK_FALSE(VerifyDigest(request, "auth-account", "3402000000", ha1, "different-nonce"));
        CHECK_FALSE(VerifyDigest(request, "device-id", "3402000000", ha1, "fixture-nonce"));
        CHECK_FALSE(VerifyDigest(request, "auth-account", "different-realm", ha1, "fixture-nonce"));
        CHECK_FALSE(VerifyDigest(request, "auth-account", "3402000000", Md5("wrong"), "fixture-nonce"));
        auto forged   = request;
        forged.method = "INVITE";
        CHECK_FALSE(VerifyDigest(forged, "auth-account", "3402000000", ha1, "fixture-nonce"));
        forged     = request;
        forged.uri = "sip:other@3402000000";
        CHECK_FALSE(VerifyDigest(forged, "auth-account", "3402000000", ha1, "fixture-nonce"));
        forged = request;
        forged.headers["authorization"] += ",nonce=\"fixture-nonce\"";
        CHECK_FALSE(VerifyDigest(forged, "auth-account", "3402000000", ha1, "fixture-nonce"));
    }
}
TEST_CASE("GB catalog separates device identity from channel identities", "[gb28181][gb-managed]") {
    auto xml = ParseXml(
        "<Response><CmdType>Catalog</CmdType><SN>12</SN><DeviceID>34020000001110000001</DeviceID><SumNum>2</"
        "SumNum><DeviceList Num=\"2\"><Item><DeviceID>34020000001320000001</DeviceID><Name>Entrance &amp; "
        "gate</Name><Status>ON</Status></Item><Item><DeviceID>34020000001320000002</DeviceID><Name>Rear</"
        "Name><Status>OFF</Status></Item></DeviceList></Response>");
    CHECK(xml.device == "34020000001110000001");
    REQUIRE(xml.channels.size() == 2);
    CHECK(xml.channels[0].id != xml.device);
    CHECK(xml.channels[0].name == "Entrance & gate");
    CHECK(xml.channels[1].status == "OFF");
    CHECK_THROWS(ParseXml("<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><Response/>"));
    CHECK_THROWS(
        ParseXml("<Response><CmdType>Catalog</CmdType><SN>1</SN><DeviceID>34020000001110000001</"
                 "DeviceID><SumNum>99999999</SumNum></Response>"));
    CHECK_THROWS(
        ParseXml("<Response><CmdType>Catalog</CmdType><SN>1</SN><DeviceID>34020000001110000001</"
                 "DeviceID><SumNum>1</SumNum><DeviceList Num=\"1\"/></Response>"));
}
TEST_CASE("GB parses GB2312 catalog names without corrupting JSON", "[gb28181][gb-managed]") {
    const auto xml = ParseXml(
        "<?xml version=\"1.0\" "
        "encoding=\"GB2312\"?><Response><CmdType>Catalog</CmdType><SN>1</SN><DeviceID>34020000001110000001</"
        "DeviceID><SumNum>1</SumNum><DeviceList "
        "Num=\"1\"><Item><DeviceID>34020000001320000001</DeviceID><Name>\xB2\xE2\xCA\xD4</Name></Item></"
        "DeviceList></Response>");
    REQUIRE(xml.channels.size() == 1);
    CHECK(xml.channels[0].name == "测试");
}
TEST_CASE("GB offers only PS video over camera-active TCP", "[gb28181][gb-managed]") {
    const auto offer = Offer("34020000002000000001", "192.0.2.10", 9001, "0200001234");
    CHECK(offer.find("a=setup:passive") != std::string::npos);
    CHECK(offer.find("m=audio") == std::string::npos);
    CHECK(AcceptsOffer(
        "v=0\r\nm=video 19000 TCP/RTP/AVP 96\r\na=sendonly\r\na=setup:active\r\na=rtpmap:96 PS/90000\r\n"));
    CHECK_FALSE(AcceptsOffer("m=video 19000 RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\n"));
    CHECK_FALSE(AcceptsOffer("m=video 0 TCP/RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\n"));
    CHECK_FALSE(AcceptsOffer("m=video 19000 TCP/RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\na=setup:passive\r\n"));
}
TEST_CASE("GB credential persistence is encrypted and fails closed on a missing key",
          "[gb28181][gb-managed]") {
    const auto directory = std::filesystem::temp_directory_path() / ("cosmo-gb-store-" + RandomHex());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{directory};
    ConfigStore store(directory.string());
    CHECK(store.Load().empty());
    const nlohmann::json config{{"secret", "fixture-private-value"}, {"enabled", true}};
    store.Save(config);
    std::ifstream input(directory / "settings.json");
    const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    CHECK(bytes.find("fixture-private-value") == std::string::npos);
    ConfigStore restarted(directory.string());
    CHECK(restarted.Load() == config);
    std::filesystem::remove(directory / "key");
    ConfigStore broken(directory.string());
    CHECK_THROWS(broken.Load());
    cosmo::camera::MsgGb28181ManageRecv request;
    nlohmann::json{{"password", "fixture-private-value"}}.get_to(request);
    CHECK(nlohmann::json(request).dump().find("fixture-private-value") == std::string::npos);
}
