#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "service/onvif/impl/OnvifProtocol.h"
#include "util/UuidUtil.h"

namespace cosmo::service::onvif {
nlohmann::json Interfaces() {
    ifaddrs* raw = nullptr;
    if (getifaddrs(&raw) != 0)
        throw std::runtime_error("network_error");
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> guard(raw, freeifaddrs);
    auto result = nlohmann::json::array();
    std::set<std::string> addresses;
    for (auto* item = raw; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET || !(item->ifa_flags & IFF_UP) ||
            !(item->ifa_flags & IFF_MULTICAST) || (item->ifa_flags & IFF_LOOPBACK))
            continue;
        char address[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(item->ifa_addr)->sin_addr, address,
                  sizeof(address));
        if (addresses.insert(address).second)
            result.push_back({{"name", item->ifa_name}, {"address", address}});
    }
    return result;
}
nlohmann::json Discover(const std::string& address, int timeoutMs) {
    const auto interfaces = Interfaces();
    struct Socket {
        int fd;
        std::string address;
        Socket(int value, std::string ip) : fd(value), address(std::move(ip)) {}
        ~Socket() {
            if (fd >= 0)
                close(fd);
        }
    };
    std::vector<std::unique_ptr<Socket>> sockets;
    const auto message_id = "urn:uuid:" + util::GenerateUUID();
    const auto probe =
        "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
        "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\"><s:Header><a:MessageID>" +
        message_id +
        "</a:MessageID><a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To><a:Action>http://"
        "schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action><a:ReplyTo><a:Address>http://"
        "schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:Address></a:ReplyTo></"
        "s:Header><s:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></s:Body></"
        "s:Envelope>";
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port   = htons(3702);
    inet_pton(AF_INET, "239.255.255.250", &target.sin_addr);
    for (const auto& item : interfaces) {
        const auto ip = item.at("address").get<std::string>();
        if (!address.empty() && ip != address)
            continue;
        const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0)
            continue;
        auto sock = std::make_unique<Socket>(fd, ip);
        if (fd >= FD_SETSIZE)
            continue;
        sockaddr_in local{};
        local.sin_family = AF_INET;
        inet_pton(AF_INET, ip.c_str(), &local.sin_addr);
        unsigned char ttl = 1;
        if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0 ||
            setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &local.sin_addr, sizeof(local.sin_addr)) != 0)
            continue;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        sockets.push_back(std::move(sock));
    }
    if (sockets.empty())
        throw std::runtime_error("no_interface");
    const auto started  = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::milliseconds(std::clamp(timeoutMs, 500, 5000));
    auto next_send      = started;
    int send_count      = 0;
    std::map<std::string, nlohmann::json> devices;
    std::array<char, 65536> buffer{};
    while (std::chrono::steady_clock::now() < deadline && devices.size() < 256) {
        if (send_count < 2 && std::chrono::steady_clock::now() >= next_send) {
            for (const auto& sock : sockets)
                sendto(sock->fd, probe.data(), probe.size(), 0, reinterpret_cast<sockaddr*>(&target),
                       sizeof(target));
            next_send += std::chrono::milliseconds(500);
            ++send_count;
        }
        fd_set set;
        FD_ZERO(&set);
        int maximum = -1;
        for (const auto& sock : sockets) {
            FD_SET(sock->fd, &set);
            maximum = std::max(maximum, sock->fd);
        }
        timeval wait{0, 100000};
        if (select(maximum + 1, &set, nullptr, nullptr, &wait) <= 0)
            continue;
        for (const auto& sock : sockets) {
            if (!FD_ISSET(sock->fd, &set))
                continue;
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            const auto size     = recvfrom(sock->fd, buffer.data(), buffer.size(), 0,
                                           reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (size <= 0 || peer.sin_family != AF_INET)
                continue;
            char sender[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &peer.sin_addr, sender, sizeof(sender)))
                continue;
            try {
                const auto document = Parse(std::string(buffer.data(), size));
                if (Text(Child(document->RootElement(), "Header"), "RelatesTo") != message_id)
                    continue;
                const auto* matches = Find(document.get(), "ProbeMatches");
                if (!matches)
                    continue;
                for (const auto* match = matches->FirstChildElement(); match;
                     match             = match->NextSiblingElement()) {
                    if (LocalName(match->Name()) != "ProbeMatch")
                        continue;
                    const auto uuid = Text(Child(match, "EndpointReference"), "Address");
                    for (const auto& endpoint : DiscoveryEndpoints(Text(match, "XAddrs"), sender)) {
                        const auto identity = uuid.empty() ? endpoint : uuid;
                        auto& device        = devices[identity];
                        if (device.is_null())
                            device = {{"uuid", uuid},
                                      {"endpoint", endpoint},
                                      {"scopes", Text(match, "Scopes")},
                                      {"endpoints", nlohmann::json::array()},
                                      {"interfaces", nlohmann::json::array()}};
                        for (const auto& pair : {std::make_pair("endpoints", endpoint),
                                                 std::make_pair("interfaces", sock->address)}) {
                            auto& list = device[pair.first];
                            if (std::find(list.begin(), list.end(), pair.second) == list.end())
                                list.push_back(pair.second);
                        }
                    }
                }
            } catch (const std::exception&) { /* Malformed/unrelated discovery traffic is ignored. */
            }
        }
    }
    auto result = nlohmann::json::array();
    for (const auto& item : devices)
        result.push_back(item.second);
    return result;
}
}  // namespace cosmo::service::onvif
