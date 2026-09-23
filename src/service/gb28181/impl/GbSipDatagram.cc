#include "service/gb28181/impl/GbSipDatagram.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace cosmo::service::gb {
namespace {
    std::string Parameter(const std::string& value, const std::string& name) {
        const auto at = value.find(";" + name + "=");
        if (at == std::string::npos)
            return "";
        const auto start = at + name.size() + 2;
        return value.substr(start, value.find_first_of(";, \t\r\n", start) - start);
    }
}  // namespace

Message SipDatagram::Parse(std::string bytes) {
    if (bytes.size() > 65507)
        throw std::runtime_error("invalid_sip");
    // Content-Length is optional for datagram SIP, unlike a TCP byte stream.
    const auto end = bytes.find("\r\n\r\n");
    if (end == std::string::npos)
        throw std::runtime_error("invalid_sip");
    auto header = bytes.substr(0, end);
    std::transform(header.begin(), header.end(), header.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (header.find("\r\ncontent-length:") == std::string::npos && header.find("\r\nl:") == std::string::npos)
        bytes.insert(end, "\r\nContent-Length: " + std::to_string(bytes.size() - end - 4));
    Message message;
    if (!Pop(bytes, message) || !bytes.empty() || message.Header("via").rfind("SIP/2.0/UDP ", 0) != 0 ||
        message.Header("via").find(',') != std::string::npos ||
        Parameter(message.Header("via"), "branch").empty())
        throw std::runtime_error("invalid_sip");
    return message;
}

void SipDatagram::RouteResponse(Message& message, const std::string& peer, int port) {
    // Direct devices only: do not relay to a caller-controlled Via/maddr target.
    auto via = message.Header("via");
    for (const auto& name : {std::string("received"), std::string("rport")}) {
        size_t at;
        while ((at = via.find(";" + name)) != std::string::npos) {
            const auto end = via.find(';', at + 1);
            via.erase(at, end == std::string::npos ? end : end - at);
        }
    }
    message.headers["via"] = via + ";received=" + peer + ";rport=" + std::to_string(port);
}

std::string SipDatagram::Key(const Message& message) {
    return message.Header("call-id") + "\n" + message.Header("cseq") + "\n" + message.Header("from") + "\n" +
           Parameter(message.Header("via"), "branch");
}
void SipDatagram::Expire(Time now) {
    for (auto it = replies_.begin(); it != replies_.end();)
        if (it->second.expires <= now)
            it = replies_.erase(it);
        else
            ++it;
}
std::string SipDatagram::Replay(const Message& request, const std::string& wire, Time now) {
    Expire(now);
    const auto it = replies_.find(Key(request));
    if (it == replies_.end())
        return "";
    if (it->second.hash != Md5(wire))
        throw std::runtime_error("invalid_sip");
    return it->second.wire;
}
void SipDatagram::Remember(const Message& request, const std::string& wire, const std::string& response,
                           Time now) {
    Expire(now);
    if (replies_.size() >= 64 && !replies_.count(Key(request))) {
        auto oldest = std::min_element(replies_.begin(), replies_.end(), [](const auto& a, const auto& b) {
            return a.second.expires < b.second.expires;
        });
        replies_.erase(oldest);
    }
    if (response.size() <= 4096)
        replies_[Key(request)] = {Md5(wire), response, now + std::chrono::seconds(32)};
}
void SipDatagram::Sent(const Message& message, const std::string& wire, Time now) {
    if (message.status || message.method == "ACK")
        return;
    if (message.method == "CANCEL") {
        auto invite            = message;
        const auto cseq        = message.Header("cseq");
        invite.headers["cseq"] = cseq.substr(0, cseq.find(' ')) + " INVITE";
        pending_.erase(Key(invite));
    }
    if (wire.size() > 8192 || pending_.size() >= 16)
        throw std::runtime_error("busy");
    pending_[Key(message)] = {wire, now + std::chrono::milliseconds(500), now + std::chrono::seconds(32)};
}
void SipDatagram::Received(const Message& response) {
    if (response.status >= 200 ||
        (response.status >= 100 && response.Header("cseq").find(" INVITE") != std::string::npos))
        pending_.erase(Key(response));
}
std::vector<std::string> SipDatagram::Due(Time now) {
    Expire(now);
    std::vector<std::string> result;
    for (auto it = pending_.begin(); it != pending_.end();) {
        auto& item = it->second;
        if (item.expires <= now) {
            it = pending_.erase(it);
            continue;
        }
        if (item.next <= now) {
            result.push_back(item.wire);
            item.interval = std::min(item.interval * 2, std::chrono::milliseconds(4000));
            item.next     = now + item.interval;
        }
        ++it;
    }
    return result;
}
}  // namespace cosmo::service::gb
