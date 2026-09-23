#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "catch_amalgamated.hpp"
#include "service/gb28181/impl/GbRtpReorder.h"
#include "service/gb28181/impl/GbSipDatagram.h"
#include "service/gb28181/impl/GbUdpMediaBridge.h"

using namespace std::chrono_literals;
namespace gb = cosmo::service::gb;
namespace {
std::string Rtp(uint16_t sequence, uint32_t ssrc = 1234) {
    std::string packet(20, '\0');
    packet[0] = char(0x80);
    packet[1] = 96;
    packet[2] = char(sequence >> 8);
    packet[3] = char(sequence & 255);
    for (int index = 0; index < 4; ++index)
        packet[8 + index] = char(ssrc >> (24 - index * 8));
    packet[15] = char(0xba);  // PS fixture payload; transport does not decode PS.
    return packet;
}
std::string Sip(const std::string& method = "MESSAGE") {
    return method +
           " sip:platform@127.0.0.1 SIP/2.0\r\nVia: SIP/2.0/UDP 127.0.0.1:5060;rport;branch=z9hG4bKtest\r\n"
           "From: <sip:camera@domain>;tag=test\r\nTo: <sip:platform@domain>\r\nCall-ID: fixture\r\nCSeq: 1 " +
           method + "\r\nContent-Length: 0\r\n\r\n";
}
struct Socket {
    int fd;
    explicit Socket(int type) : fd(socket(AF_INET, type, 0)) {
        REQUIRE(fd >= 0);
    }
    ~Socket() {
        close(fd);
    }
};
}  // namespace
TEST_CASE("GB UDP SDP remains independent of signaling and TCP stays the default", "[gb28181][gb-udp]") {
    auto sdp = gb::Offer("34020000002000000001", "192.0.2.1", 9001, "0000001234", true);
    CHECK(sdp.find(" RTP/AVP 96") != std::string::npos);
    CHECK(sdp.find("setup:") == std::string::npos);
    CHECK(sdp.find("connection:") == std::string::npos);
    const std::string answer = "m=video 9001 RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\na=sendonly\r\n";
    CHECK(gb::AcceptsOffer(answer, true));
    CHECK_FALSE(gb::AcceptsOffer(answer));
    CHECK_FALSE(gb::AcceptsOffer("m=video 9001 TCP/RTP/AVP 96\r\na=rtpmap:96 PS/90000\r\n", true));
}
TEST_CASE("GB SIP datagram framing rejects concatenation and handles absent length", "[gb28181][gb-udp]") {
    auto wire = Sip();
    CHECK(gb::SipDatagram::Parse(wire).method == "MESSAGE");
    CHECK_THROWS(gb::SipDatagram::Parse(wire + wire));
    wire.erase(wire.find("Content-Length: 0\r\n"), 19);
    CHECK(gb::SipDatagram::Parse(wire).body.empty());
    CHECK_THROWS(gb::SipDatagram::Parse("truncated"));
    auto request = gb::SipDatagram::Parse(Sip());
    gb::SipDatagram::RouteResponse(request, "192.0.2.5", 12345);
    CHECK(request.Header("via").find(";received=192.0.2.5;rport=12345") != std::string::npos);
}
TEST_CASE("GB UDP transactions cache exact duplicates and have bounded retransmission", "[gb28181][gb-udp]") {
    gb::SipDatagram transactions;
    const auto now = gb::SipDatagram::Clock::now();
    auto wire      = Sip();
    auto message   = gb::SipDatagram::Parse(wire);
    CHECK(transactions.Replay(message, wire, now).empty());
    transactions.Remember(message, wire, "cached response", now);
    CHECK(transactions.Replay(message, wire, now + 1s) == "cached response");
    CHECK_THROWS(transactions.Replay(message, wire + "tampered", now));
    CHECK(transactions.Replay(message, wire, now + 33s).empty());
    transactions.Sent(message, wire, now);
    CHECK(transactions.Due(now + 499ms).empty());
    REQUIRE(transactions.Due(now + 500ms) == std::vector<std::string>{wire});
    CHECK(transactions.Due(now + 1499ms).empty());
    CHECK(transactions.Due(now + 1500ms).size() == 1);
    auto response = gb::SipDatagram::Parse(gb::Response(message, 200));
    transactions.Received(response);
    CHECK(transactions.Due(now + 5s).empty());
    transactions.Sent(message, wire, now);
    CHECK(transactions.Due(now + 33s).empty());
}
TEST_CASE("GB RTP reorder handles duplicates gaps and sequence wrap with bounded wait", "[gb28181][gb-udp]") {
    gb::RtpReorder queue;
    auto now = std::chrono::steady_clock::now();
    CHECK(queue.Push(Rtp(65535), now));
    REQUIRE(queue.Pop(now) == std::vector<std::string>{Rtp(65535)});
    CHECK(queue.Push(Rtp(1), now));
    CHECK_FALSE(queue.Push(Rtp(1), now));
    CHECK(queue.Pop(now + 10ms).empty());
    CHECK(queue.Push(Rtp(0), now + 11ms));
    CHECK(queue.Pop(now + 11ms) == std::vector<std::string>{Rtp(0), Rtp(1)});
    CHECK_FALSE(queue.Push(Rtp(65535), now));
    CHECK(queue.Push(Rtp(3), now));
    CHECK(queue.Pop(now + 49ms).empty());
    CHECK(queue.Pop(now + 50ms) == std::vector<std::string>{Rtp(3)});
    uint32_t ssrc;
    uint16_t sequence;
    CHECK_FALSE(gb::RtpReorder::Header("short", ssrc, sequence));
    auto bad = Rtp(10);
    bad[0]   = char(0x9f);
    CHECK_FALSE(gb::RtpReorder::Header(bad, ssrc, sequence));
    bad    = Rtp(10);
    bad[1] = 97;
    CHECK_FALSE(gb::RtpReorder::Header(bad, ssrc, sequence));
    for (int index = 4; index < 1000; ++index)
        queue.Push(Rtp(index), now);
    CHECK(queue.Pop(now + 1s).size() <= 64);
}
TEST_CASE("GB managed UDP ingress feeds only allocated RTP into loopback RFC4571", "[gb28181][gb-udp]") {
    Socket listener(SOCK_STREAM), sender(SOCK_DGRAM), foreign(SOCK_DGRAM);
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    REQUIRE(listen(listener.fd, 4) == 0);
    socklen_t size = sizeof(address);
    REQUIRE(getsockname(listener.fd, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    gb::UdpMediaBridge bridge(ntohs(address.sin_port));
    bridge.Add(1234, "127.0.0.1");
    CHECK_THROWS(bridge.Add(1234, "127.0.0.1"));
    pollfd ready{listener.fd, POLLIN, 0};
    REQUIRE(poll(&ready, 1, 2000) == 1);
    const int client = accept(listener.fd, nullptr, nullptr);
    REQUIRE(client >= 0);
    struct Cleanup {
        int fd;
        ~Cleanup() {
            close(fd);
        }
    } cleanup{client};
    sockaddr_in foreignAddress{};
    foreignAddress.sin_family = AF_INET;
    REQUIRE(inet_pton(AF_INET, "127.0.0.2", &foreignAddress.sin_addr) == 1);
    REQUIRE(bind(foreign.fd, reinterpret_cast<sockaddr*>(&foreignAddress), sizeof(foreignAddress)) == 0);
    const auto foreignPacket = Rtp(100);
    REQUIRE(sendto(foreign.fd, foreignPacket.data(), foreignPacket.size(), 0,
                   reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 20);
    for (auto packet : {Rtp(0, 9999), Rtp(1), Rtp(3), Rtp(2), Rtp(2)})
        REQUIRE(sendto(sender.fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&address),
                       sizeof(address)) == 20);
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (output.size() < 66 && std::chrono::steady_clock::now() < deadline) {
        ready = {client, POLLIN, 0};
        if (poll(&ready, 1, 100) <= 0)
            continue;
        char buffer[1000];
        const auto count = recv(client, buffer, sizeof(buffer), 0);
        REQUIRE(count > 0);
        output.append(buffer, count);
    }
    std::string expected;
    for (int sequence : {1, 2, 3})
        expected += std::string("\0\24", 2) + Rtp(sequence);
    CHECK(output == expected);
    bridge.Remove(1234);
    ready = {client, POLLIN, 0};
    REQUIRE(poll(&ready, 1, 2000) > 0);
    char byte;
    CHECK(recv(client, &byte, 1, 0) == 0);
}
