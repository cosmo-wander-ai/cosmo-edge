#pragma once

#include <map>
#include <string>
#include <vector>

namespace cosmo::service::gb {
// This bounded TCP subset is intentionally not a general SIP proxy.
struct Message {
    std::string method, uri, body;
    int status{0};
    std::map<std::string, std::string> headers;
    std::string Header(const std::string& name) const;
};
bool IsId(const std::string& value);
bool IsIpv4(const std::string& value);
std::string RandomHex(size_t bytes = 16);
std::string Md5(const std::string& value);
std::string User(const std::string& address);
std::string Uri(const std::string& address);
// Returns false only for an incomplete frame; malformed/oversized input throws.
bool Pop(std::string& buffer, Message& message);
std::string Response(const Message& request, int status, const std::string& extra = "");
bool VerifyDigest(const Message& request, const std::string& username, const std::string& realm,
                  const std::string& ha1, const std::string& nonce);
struct Channel {
    std::string id, name, status;
};
struct XmlMessage {
    std::string command, device, sn;
    int total{0};
    std::vector<Channel> channels;
};
XmlMessage ParseXml(const std::string& body);
std::string CatalogQuery(const std::string& platformId, const std::string& sn);
std::string Offer(const std::string& platformId, const std::string& address, int port,
                  const std::string& ssrc);
bool AcceptsOffer(const std::string& sdp);
}  // namespace cosmo::service::gb
