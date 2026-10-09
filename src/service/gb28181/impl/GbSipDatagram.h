#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "service/gb28181/impl/GbSipProtocol.h"

namespace cosmo::service::gb {
// Owned by one SIP session in the management worker. Bounded duplicate response
// cache and client retransmissions; no socket or timer thread of its own.
class SipDatagram {
public:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;
    static Message Parse(std::string bytes);
    static void RouteResponse(Message& message, const std::string& peer, int port);
    // Call before applying any request side effects, including nonce consumption.
    std::string Replay(const Message& request, const std::string& wire, Time now);
    void Remember(const Message& request, const std::string& wire, const std::string& response, Time now);
    void Sent(const Message& message, const std::string& wire, Time now);
    void Received(const Message& response);
    std::vector<std::string> Due(Time now);

private:
    struct Reply {
        std::string hash, wire;
        Time expires;
    };
    struct Pending {
        std::string wire;
        Time next, expires;
        std::chrono::milliseconds interval{500};
    };
    static std::string Key(const Message& message);
    void Expire(Time now);
    std::map<std::string, Reply> replies_;
    std::map<std::string, Pending> pending_;
};
}  // namespace cosmo::service::gb
