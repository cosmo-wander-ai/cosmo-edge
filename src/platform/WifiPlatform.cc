#include "platform/WifiPlatform.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "cryptopp/pwdbased.h"
#include "cryptopp/sha.h"
#include "util/Exec.h"

namespace cosmo::platform {
namespace {
    constexpr auto kCommandTimeout     = std::chrono::milliseconds(3000);
    constexpr const char* kWiredDropIn = "/etc/systemd/network/10-netplan-eth0.network.d/80-cosmo-wifi.conf";
    std::atomic<unsigned> socket_sequence{0};

    bool ValidName(const std::string& name) {
        return !name.empty() && name.size() < IFNAMSIZ &&
               std::all_of(name.begin(), name.end(), [](unsigned char c) {
                   return std::isalnum(c) || c == '_' || c == '-' || c == '.';
               });
    }
    uint32_t IpValue(const std::string& value) {
        in_addr address{};
        if (inet_pton(AF_INET, value.c_str(), &address) != 1)
            throw std::runtime_error("invalidIpv4");
        return ntohl(address.s_addr);
    }
    bool UsableIp(const std::string& value) {
        try {
            const auto ip = IpValue(value);
            return (ip >> 24) != 0 && (ip >> 24) != 127 && ip < 0xe0000000U;
        } catch (...) {
            return false;
        }
    }
    int Prefix(const std::string& value) {
        auto mask = IpValue(value);
        if (mask == 0 || (~mask & (~mask + 1U)) != 0)
            throw std::runtime_error("invalidNetMask");
        int prefix = 0;
        while (mask) {
            ++prefix;
            mask <<= 1;
        }
        if (prefix > 30)
            throw std::runtime_error("invalidNetMask");
        return prefix;
    }
    std::string Hex(const std::string& value) {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        for (unsigned char c : value) {
            result += digits[c >> 4];
            result += digits[c & 15];
        }
        return result;
    }
    std::string DecodeSsid(const std::string& encoded) {
        std::string decoded;
        for (size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] == '\\' && i + 1 < encoded.size()) {
                const char next = encoded[++i];
                if (next == 'x' && i + 2 < encoded.size() &&
                    std::isxdigit(static_cast<unsigned char>(encoded[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(encoded[i + 2]))) {
                    decoded += static_cast<char>(std::stoul(encoded.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else if (next == 'n') {
                    decoded += '\n';
                } else if (next == 'r') {
                    decoded += '\r';
                } else if (next == 't') {
                    decoded += '\t';
                } else {
                    decoded += next;
                }
            } else {
                decoded += encoded[i];
            }
        }
        return decoded;
    }
    std::string Trim(std::string value) {
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
            value.pop_back();
        return value;
    }
    int Run(const std::vector<std::string>& args, std::string& output) {
        std::vector<std::string> bounded{"timeout", "10s"};
        bounded.insert(bounded.end(), args.begin(), args.end());
        return util::ExecWithOutputLimit(bounded, output, 128 * 1024);
    }
    void RunChecked(const std::vector<std::string>& args, const char* error) {
        std::string output;
        if (Run(args, output) != 0)
            throw std::runtime_error(error);
    }

    std::vector<std::string> ResolvedValues(const std::string& property, const std::string& interface_name) {
        std::string output;
        if (Run({"resolvectl", property, interface_name}, output) != 0)
            throw std::runtime_error("dnsConfigFailed");
        const auto delimiter = output.find(':');
        if (delimiter == std::string::npos)
            throw std::runtime_error("dnsConfigFailed");
        std::istringstream values(output.substr(delimiter + 1));
        std::vector<std::string> result;
        std::string value;
        while (values >> value)
            result.push_back(value);
        return result;
    }
    void SetResolvedValues(const std::string& property, const std::string& interface_name,
                           const std::vector<std::string>& values) {
        std::vector<std::string> args{"resolvectl", property, interface_name};
        const auto normalized = NormalizeResolvedValuesForLink(property, values);
        args.insert(args.end(), normalized.begin(), normalized.end());
        if (normalized.empty())
            args.emplace_back("");
        RunChecked(args, "dnsConfigFailed");
    }

    void WriteNetworkFile(const std::string& path, const std::string& contents) {
        WritePrivateFile(path, contents);
        // networkd reads these non-secret files after dropping root privileges.
        if (chmod(path.c_str(), 0644) != 0)
            throw std::runtime_error("saveFailed");
    }

    class ControlSocket final {
    public:
        explicit ControlSocket(const std::string& destination) {
            fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if (fd_ < 0)
                throw std::runtime_error("serviceUnavailable");
            local_ = "/tmp/cosmo-wpa-" + std::to_string(getpid()) + "-" +
                     std::to_string(socket_sequence.fetch_add(1));
            sockaddr_un local_address{};
            local_address.sun_family = AF_UNIX;
            std::memcpy(local_address.sun_path, local_.c_str(), local_.size() + 1);
            sockaddr_un remote{};
            remote.sun_family = AF_UNIX;
            if (destination.size() >= sizeof(remote.sun_path)) {
                Close();
                throw std::runtime_error("serviceUnavailable");
            }
            std::memcpy(remote.sun_path, destination.c_str(), destination.size() + 1);
            if (bind(fd_, reinterpret_cast<sockaddr*>(&local_address), sizeof(local_address)) != 0 ||
                chmod(local_.c_str(), 0600) != 0 ||
                connect(fd_, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) != 0) {
                Close();
                throw std::runtime_error("serviceUnavailable");
            }
        }
        ~ControlSocket() {
            if (attached_ && fd_ >= 0)
                send(fd_, "DETACH", 6, MSG_DONTWAIT);
            Close();
        }
        ControlSocket(const ControlSocket&)            = delete;
        ControlSocket& operator=(const ControlSocket&) = delete;
        std::string Request(const std::string& command) {
            if (send(fd_, command.data(), command.size(), 0) != static_cast<ssize_t>(command.size()))
                throw std::runtime_error("controlFailed");
            auto response = Receive(kCommandTimeout);
            if (response.empty())
                throw std::runtime_error("controlTimeout");
            if (command == "ATTACH" && response.find("OK") == 0)
                attached_ = true;
            return response;
        }
        std::string Receive(std::chrono::milliseconds timeout) {
            pollfd descriptor{fd_, POLLIN, 0};
            int result;
            do {
                result = poll(&descriptor, 1, static_cast<int>(timeout.count()));
            } while (result < 0 && errno == EINTR);
            if (result == 0)
                return {};
            if (result < 0 || !(descriptor.revents & POLLIN))
                throw std::runtime_error("controlFailed");
            std::array<char, 65536> response{};
            const auto count = recv(fd_, response.data(), response.size(), 0);
            if (count < 0)
                throw std::runtime_error("controlFailed");
            return std::string(response.data(), static_cast<size_t>(count));
        }

    private:
        void Close() {
            if (fd_ >= 0)
                close(fd_);
            fd_ = -1;
            if (!local_.empty())
                unlink(local_.c_str());
        }
        int fd_{-1};
        bool attached_{false};
        std::string local_;
    };

    class NativeWifiPlatform final : public IWifiPlatform {
    public:
        std::vector<NetworkInterfaceState> Interfaces() override {
            return ReadNetworkInterfaces();
        }
        bool IsSupported() const override {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
            return true;
#else
            return false;
#endif
        }
        nlohmann::json Snapshot(const std::string& interface_name) override {
            nlohmann::json files = nlohmann::json::object();
            for (const auto& file :
                 {SupplicantFile(interface_name), paths_.network_file, std::string(kWiredDropIn)}) {
                files[file] = std::filesystem::exists(file) ? nlohmann::json(ReadPrivateFile(file))
                                                            : nlohmann::json(nullptr);
            }
            nlohmann::json units = nlohmann::json::object();
            for (const auto& unit : {Unit(interface_name), std::string("wpa_supplicant.service")}) {
                std::string enabled;
                std::string active;
                Run({"systemctl", "is-enabled", unit}, enabled);
                Run({"systemctl", "is-active", unit}, active);
                units[unit] = {{"enabled", Trim(enabled)}, {"active", Trim(active) == "active"}};
            }
            nlohmann::json routes = nlohmann::json::array();
            for (const auto& info : ReadNetworkInterfaces()) {
                if (info.interface_name == "eth0" && !info.gateway.empty() && info.metric >= 0)
                    routes.push_back({{"gateway", info.gateway}, {"metric", info.metric}});
            }
            auto dns = nlohmann::json::array();
            for (const auto& info : ReadNetworkInterfaces()) {
                if (info.interface_name != "eth0" && info.interface_name != interface_name)
                    continue;
                const auto defaults = ResolvedValues("default-route", info.interface_name);
                dns.push_back({{"interfaceName", info.interface_name},
                               {"dns", ResolvedValues("dns", info.interface_name)},
                               {"domains", ResolvedValues("domain", info.interface_name)},
                               {"defaultRoute", !defaults.empty() && defaults.front() == "yes"}});
            }
            return {{"files", files}, {"units", units}, {"wiredRoutes", routes}, {"resolved", dns}};
        }
        void Prepare(const std::string& interface_name) override {
            CheckInterface(interface_name);
            std::string generic;
            Run({"systemctl", "show", "-p", "ExecStart", "wpa_supplicant.service"}, generic);
            if (generic.find("wlan0") != std::string::npos && if_nametoindex("wlan0") == 0) {
                RunChecked({"systemctl", "disable", "--now", "wpa_supplicant.service"},
                           "servicePreparationFailed");
            }
            if (!std::filesystem::exists(SupplicantFile(interface_name)))
                WritePrivateFile(SupplicantFile(interface_name),
                                 "ctrl_interface=/run/wpa_supplicant\nupdate_config=0\n");
            RunChecked({"ip", "link", "set", "dev", interface_name, "up"}, "radioUnavailable");
            RunChecked({"systemctl", "start", Unit(interface_name)}, "serviceUnavailable");
            for (int i = 0; i < 30; ++i) {
                try {
                    if (Request(interface_name, "PING").find("PONG") == 0)
                        return;
                } catch (...) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            throw std::runtime_error("serviceUnavailable");
        }
        std::string Request(const std::string& interface_name, const std::string& command) override {
            CheckInterface(interface_name);
            ControlSocket socket(paths_.control_dir + "/" + interface_name);
            return socket.Request(command);
        }
        std::string WaitEvent(const std::string& interface_name, std::chrono::milliseconds timeout) override {
            CheckInterface(interface_name);
            if (!events_ || event_interface_ != interface_name) {
                events_ = std::make_unique<ControlSocket>(paths_.control_dir + "/" + interface_name);
                if (events_->Request("ATTACH").find("OK") != 0) {
                    events_.reset();
                    throw std::runtime_error("controlFailed");
                }
                event_interface_ = interface_name;
            }
            return events_->Receive(timeout);
        }
        void Apply(const std::string& interface_name, const nlohmann::json& settings) override {
            CheckInterface(interface_name);
            WritePrivateFile(SupplicantFile(interface_name), WifiSupplicantConfig(settings, true));
            WriteNetworkFile(paths_.network_file, WifiNetworkConfig(interface_name, settings));
            WriteNetworkFile(kWiredDropIn, "[DHCPv4]\nRouteMetric=100\n");
            RunChecked({"networkctl", "reload"}, "addressConfigFailed");
            ReconfigureAndReconnectWifi(
                [&](const std::string& command) { return Request(interface_name, command); });
            RunChecked({"ip", "address", "flush", "dev", interface_name, "scope", "global"},
                       "addressConfigFailed");
            RunChecked({"networkctl", "reconfigure", interface_name}, "addressConfigFailed");
            // Route addition precedes deletion, preserving the wired maintenance path.
            for (const auto& info : ReadNetworkInterfaces()) {
                if (info.interface_name != "eth0" || info.gateway.empty() || info.metric == 100)
                    continue;
                RunChecked({"ip", "route", "replace", "default", "via", info.gateway, "dev", "eth0", "metric",
                            "100"},
                           "routeConfigFailed");
                RunChecked({"ip", "route", "del", "default", "via", info.gateway, "dev", "eth0", "metric",
                            std::to_string(info.metric)},
                           "routeConfigFailed");
            }
        }
        void Restore(const std::string& interface_name, const nlohmann::json& snapshot) override {
            events_.reset();
            for (const auto& entry : snapshot.at("files").items()) {
                // Snapshot paths are accepted only from this application's managed set.
                if (entry.key() != SupplicantFile(interface_name) && entry.key() != paths_.network_file &&
                    entry.key() != kWiredDropIn)
                    throw std::runtime_error("rollbackFailed");
                if (entry.value().is_null()) {
                    std::filesystem::remove(entry.key());
                } else {
                    if (entry.key() == SupplicantFile(interface_name))
                        WritePrivateFile(entry.key(), entry.value().get<std::string>());
                    else
                        WriteNetworkFile(entry.key(), entry.value().get<std::string>());
                }
            }
            RunChecked({"networkctl", "reload"}, "rollbackFailed");
            std::string ignored;
            Run({"networkctl", "reconfigure", interface_name}, ignored);
            for (const auto& entry : snapshot.at("units").items()) {
                if (entry.key() != Unit(interface_name) && entry.key() != "wpa_supplicant.service")
                    throw std::runtime_error("rollbackFailed");
                const auto enabled = entry.value().value("enabled", "disabled");
                if (enabled == "enabled" || enabled == "enabled-runtime") {
                    RunChecked({"systemctl", "enable", entry.key()}, "rollbackFailed");
                } else if (enabled != "static" && enabled != "indirect" && enabled != "masked") {
                    RunChecked({"systemctl", "disable", entry.key()}, "rollbackFailed");
                }
                RunChecked(
                    {"systemctl", entry.value().value("active", false) ? "restart" : "stop", entry.key()},
                    "rollbackFailed");
            }
            for (const auto& route : snapshot.value("wiredRoutes", nlohmann::json::array())) {
                const auto gateway = route.at("gateway").get<std::string>();
                const auto metric  = route.at("metric").get<int>();
                if (!UsableIp(gateway) || metric < 0)
                    throw std::runtime_error("rollbackFailed");
                RunChecked({"ip", "route", "replace", "default", "via", gateway, "dev", "eth0", "metric",
                            std::to_string(metric)},
                           "rollbackFailed");
                if (metric != 100)
                    Run({"ip", "route", "del", "default", "via", gateway, "dev", "eth0", "metric", "100"},
                        ignored);
            }
            if (snapshot.at("files").at(paths_.network_file).is_null()) {
                RunChecked({"ip", "address", "flush", "dev", interface_name, "scope", "global"},
                           "rollbackFailed");
            }
            for (const auto& dns : snapshot.value("resolved", nlohmann::json::array())) {
                const auto name = dns.at("interfaceName").get<std::string>();
                if (name != "eth0" && name != interface_name)
                    throw std::runtime_error("rollbackFailed");
                SetResolvedValues("dns", name, dns.at("dns").get<std::vector<std::string>>());
                SetResolvedValues("domain", name, dns.at("domains").get<std::vector<std::string>>());
                RunChecked(
                    {"resolvectl", "default-route", name, dns.value("defaultRoute", false) ? "yes" : "no"},
                    "rollbackFailed");
            }
        }
        void Commit(const std::string& interface_name, const nlohmann::json& settings,
                    bool auto_connect) override {
            WritePrivateFile(SupplicantFile(interface_name), WifiSupplicantConfig(settings, auto_connect));
            RunChecked({"systemctl", auto_connect ? "enable" : "disable", Unit(interface_name)},
                       "saveFailed");
        }
        void Clear(const std::string& interface_name, bool forget) override {
            std::string ignored;
            if (!forget) {
                if (Request(interface_name, "DISCONNECT").find("OK") != 0)
                    throw std::runtime_error("disconnectFailed");
            } else {
                events_.reset();
                bool control_ready = false;
                try {
                    control_ready = Request(interface_name, "REMOVE_NETWORK all").find("OK") == 0;
                } catch (...) {
                }
                if (!control_ready)
                    RunChecked({"systemctl", "stop", Unit(interface_name)}, "forgetFailed");
                // Keep an empty control endpoint for subsequent scans, with no boot enablement.
                WritePrivateFile(SupplicantFile(interface_name),
                                 "ctrl_interface=/run/wpa_supplicant\nupdate_config=0\n");
                RunChecked({"systemctl", "disable", Unit(interface_name)}, "forgetFailed");
                std::filesystem::remove(paths_.network_file);
                RunChecked({"networkctl", "reload"}, "forgetFailed");
            }
            RunChecked({"ip", "address", "flush", "dev", interface_name, "scope", "global"},
                       "disconnectFailed");
        }
        bool HasAddressConflict(const std::string& interface_name, const std::string& address) override {
            CheckInterface(interface_name);
            const auto index = if_nametoindex(interface_name.c_str());
            const int fd     = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ARP));
            if (fd < 0 || index == 0) {
                if (fd >= 0)
                    close(fd);
                throw std::runtime_error("addressConflictCheckUnavailable");
            }
            struct SocketCloser {
                int fd;
                ~SocketCloser() {
                    close(fd);
                }
            } socket_closer{fd};
            ifreq request{};
            std::memcpy(request.ifr_name, interface_name.c_str(), interface_name.size() + 1);
            if (ioctl(fd, SIOCGIFHWADDR, &request) != 0)
                throw std::runtime_error("addressConflictCheckFailed");
            ifaddrs* raw_interfaces = nullptr;
            if (getifaddrs(&raw_interfaces) != 0)
                throw std::runtime_error("addressConflictCheckFailed");
            const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> local_interfaces(raw_interfaces,
                                                                                    freeifaddrs);
            std::vector<std::array<unsigned char, 6>> local_macs;
            for (auto* entry = raw_interfaces; entry; entry = entry->ifa_next) {
                if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_PACKET)
                    continue;
                const auto* link = reinterpret_cast<const sockaddr_ll*>(entry->ifa_addr);
                if (link->sll_halen != 6)
                    continue;
                std::array<unsigned char, 6> mac{};
                std::copy_n(link->sll_addr, mac.size(), mac.begin());
                local_macs.push_back(mac);
            }
            std::array<unsigned char, 42> packet{};
            std::fill_n(packet.begin(), 6, 0xff);
            std::copy_n(reinterpret_cast<unsigned char*>(request.ifr_hwaddr.sa_data), 6, packet.begin() + 6);
            // Ethernet header followed by an RFC 5227 ARP probe (sender IPv4 is zero).
            packet[12] = 0x08;
            packet[13] = 0x06;
            packet[14] = 0;
            packet[15] = 1;
            packet[16] = 0x08;
            packet[17] = 0;
            packet[18] = 6;
            packet[19] = 4;
            packet[20] = 0;
            packet[21] = 1;
            std::copy_n(packet.begin() + 6, 6, packet.begin() + 22);
            in_addr candidate{};
            if (inet_pton(AF_INET, address.c_str(), &candidate) != 1)
                throw std::runtime_error("invalidIpv4");
            std::memcpy(packet.data() + 38, &candidate.s_addr, 4);
            std::array<unsigned char, 4> candidate_bytes{};
            std::memcpy(candidate_bytes.data(), &candidate.s_addr, candidate_bytes.size());
            sockaddr_ll destination{};
            destination.sll_family   = AF_PACKET;
            destination.sll_protocol = htons(ETH_P_ARP);
            destination.sll_ifindex  = static_cast<int>(index);
            destination.sll_halen    = 6;
            std::fill_n(destination.sll_addr, 6, 0xff);
            if (bind(fd, reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) != 0)
                throw std::runtime_error("addressConflictCheckFailed");
            for (int probe = 0; probe < 3; ++probe) {
                if (sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&destination),
                           sizeof(destination)) != static_cast<ssize_t>(packet.size()))
                    throw std::runtime_error("addressConflictCheckFailed");
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
                while (std::chrono::steady_clock::now() < deadline) {
                    pollfd descriptor{fd, POLLIN, 0};
                    const int count = poll(&descriptor, 1, 50);
                    if (count < 0 && errno != EINTR)
                        throw std::runtime_error("addressConflictCheckFailed");
                    if (count <= 0)
                        continue;
                    std::array<unsigned char, 2048> response{};
                    const auto length = recv(fd, response.data(), response.size(), MSG_DONTWAIT);
                    if (length > 0 && IsConflictingArpPacket(response.data(), static_cast<size_t>(length),
                                                             candidate_bytes, local_macs))
                        return true;
                }
            }
            return false;
        }
        void UpdateDnsRoute(const std::string& current_interface) override {
            for (const auto& info : ReadNetworkInterfaces()) {
                if (info.interface_name != "eth0" && info.type != "wifi")
                    continue;
                RunChecked({"resolvectl", "default-route", info.interface_name,
                            info.interface_name == current_interface ? "yes" : "no"},
                           "dnsConfigFailed");
                auto domains = ResolvedValues("domain", info.interface_name);
                domains.erase(std::remove(domains.begin(), domains.end(), "~."), domains.end());
                if (info.interface_name == current_interface)
                    domains.emplace_back("~.");
                SetResolvedValues("domain", info.interface_name, domains);
            }
        }

    private:
        void CheckInterface(const std::string& name) const {
            if (!ValidName(name))
                throw std::runtime_error("invalidInterface");
        }
        std::string Unit(const std::string& name) const {
            CheckInterface(name);
            return "wpa_supplicant-nl80211@" + name + ".service";
        }
        std::string SupplicantFile(const std::string& name) const {
            CheckInterface(name);
            return paths_.supplicant_dir + "/wpa_supplicant-nl80211-" + name + ".conf";
        }
        WifiPlatformPaths paths_;
        std::unique_ptr<ControlSocket> events_;
        std::string event_interface_;
    };
}  // namespace

bool IsConflictingArpPacket(const unsigned char* packet, size_t length,
                            const std::array<unsigned char, 4>& candidate,
                            const std::vector<std::array<unsigned char, 6>>& local_macs) {
    if (!packet || length < 42 || packet[12] != 0x08 || packet[13] != 0x06 || packet[14] != 0 ||
        packet[15] != 1 || packet[16] != 0x08 || packet[17] != 0 || packet[18] != 6 || packet[19] != 4 ||
        packet[20] != 0 || (packet[21] != 1 && packet[21] != 2))
        return false;
    // Linux may answer for the Wi-Fi IPv4 on another local interface on the same subnet.
    if (std::any_of(local_macs.begin(), local_macs.end(),
                    [&](const auto& mac) { return std::equal(mac.begin(), mac.end(), packet + 22); }))
        return false;
    const bool sender_matches = std::equal(candidate.begin(), candidate.end(), packet + 28);
    const std::array<unsigned char, 4> zero{};
    const bool competing_probe = packet[21] == 1 && std::equal(zero.begin(), zero.end(), packet + 28) &&
                                 std::equal(candidate.begin(), candidate.end(), packet + 38);
    return sender_matches || competing_probe;
}

void ReconfigureAndReconnectWifi(const std::function<std::string(const std::string&)>& request) {
    if (request("RECONFIGURE").find("OK") != 0)
        throw std::runtime_error("authenticationConfigFailed");
    // RECONFIGURE retains an explicit DISCONNECT; RECONNECT clears that state.
    if (request("RECONNECT").find("OK") != 0)
        throw std::runtime_error("authenticationConfigFailed");
}

std::string DecodeWifiSsid(const std::string& encoded) {
    return DecodeSsid(encoded);
}

std::vector<std::string> NormalizeResolvedValuesForLink(const std::string& property,
                                                        const std::vector<std::string>& values) {
    auto normalized = values;
    if (property != "dns")
        return normalized;
    for (auto& value : normalized) {
        const auto scope = value.find('%');
        if (scope == std::string::npos)
            continue;
        const auto address = value.substr(0, scope);
        in6_addr ipv6{};
        // The resolvectl command already names the current link; saved ifindices can expire.
        if (inet_pton(AF_INET6, address.c_str(), &ipv6) == 1)
            value = address;
    }
    return normalized;
}

std::vector<NetworkInterfaceState> ReadNetworkInterfaces() {
    std::vector<NetworkInterfaceState> result;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/net", ec)) {
        const auto name = entry.path().filename().string();
        if (!ValidName(name) || name == "lo")
            continue;
        const bool wireless = std::filesystem::exists(entry.path() / "wireless") ||
                              std::filesystem::exists(entry.path() / "phy80211");
        if (!wireless && name != "eth0" && name != "eth1")
            continue;
        NetworkInterfaceState info;
        info.interface_name = name;
        info.type           = wireless ? "wifi" : "ethernet";
        std::ifstream carrier(entry.path() / "carrier");
        int value    = 0;
        info.link_up = static_cast<bool>(carrier >> value) && value == 1;
        result.push_back(std::move(info));
    }
    ifaddrs* raw = nullptr;
    if (getifaddrs(&raw) == 0) {
        const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> addresses(raw, freeifaddrs);
        for (auto* entry = raw; entry; entry = entry->ifa_next) {
            if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
                continue;
            auto found = std::find_if(result.begin(), result.end(), [&](const auto& info) {
                return info.interface_name == entry->ifa_name;
            });
            if (found == result.end())
                continue;
            char address[INET_ADDRSTRLEN]{};
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address));
            if (!UsableIp(address))
                continue;
            found->ip_addr = address;
            if (entry->ifa_netmask) {
                const auto* mask = reinterpret_cast<const sockaddr_in*>(entry->ifa_netmask);
                inet_ntop(AF_INET, &mask->sin_addr, address, sizeof(address));
                found->net_mask = address;
            }
        }
    }
    std::ifstream routes("/proc/net/route");
    std::string line;
    std::getline(routes, line);
    int best_metric = std::numeric_limits<int>::max();
    std::string current;
    while (std::getline(routes, line)) {
        std::istringstream row(line);
        std::string name;
        uint32_t destination = 0, gateway = 0, flags = 0;
        unsigned ref_count = 0, use = 0;
        int metric = 0;
        row >> name >> std::hex >> destination >> gateway >> flags >> std::dec >> ref_count >> use >> metric;
        if (!row || destination != 0 || !(flags & 1U))
            continue;
        auto found = std::find_if(result.begin(), result.end(),
                                  [&](const auto& info) { return info.interface_name == name; });
        if (found == result.end() || !found->link_up || found->ip_addr.empty() || name == "eth1")
            continue;
        if (found->metric < 0 || metric < found->metric) {
            in_addr address{gateway};
            char text[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &address, text, sizeof(text));
            found->gateway = gateway == 0 ? "" : text;
            found->metric  = metric;
        }
        if (metric < best_metric) {
            best_metric = metric;
            current     = name;
        }
    }
    for (auto& info : result)
        info.is_default = info.interface_name == current;
    return result;
}
std::string CurrentHostAddress() {
    const auto interfaces = ReadNetworkInterfaces();
    for (const auto& info : interfaces)
        if (info.is_default)
            return info.ip_addr;
    for (const auto& info : interfaces)
        if (info.link_up && !info.ip_addr.empty())
            return info.ip_addr;
    return {};
}
std::string AddressForPeer(const std::string& ipv4) {
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port   = htons(9);
    if (inet_pton(AF_INET, ipv4.c_str(), &destination.sin_addr) != 1)
        return {};
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return {};
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    std::string result;
    if (connect(fd, reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) == 0 &&
        getsockname(fd, reinterpret_cast<sockaddr*>(&local), &size) == 0) {
        char text[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)))
            result = text;
    }
    close(fd);
    return result;
}
bool HasWirelessInterface() {
    const auto interfaces = ReadNetworkInterfaces();
    return std::any_of(interfaces.begin(), interfaces.end(),
                       [](const auto& info) { return info.type == "wifi"; });
}
bool SetResolvedDns(const std::string& interface_name, const std::vector<std::string>& dns) {
    if (!ValidName(interface_name) ||
        !std::all_of(dns.begin(), dns.end(), [](const auto& value) { return UsableIp(value); }))
        return false;
    std::vector<std::string> args{"resolvectl", "dns", interface_name};
    args.insert(args.end(), dns.begin(), dns.end());
    if (dns.empty())
        args.emplace_back("");
    std::string output;
    return Run(args, output) == 0;
}
nlohmann::json NetworkInterfacesJson(const std::vector<NetworkInterfaceState>& interfaces) {
    auto result = nlohmann::json::array();
    for (const auto& info : interfaces)
        result.push_back({{"interfaceName", info.interface_name},
                          {"type", info.type},
                          {"linkUp", info.link_up},
                          {"ipAddr", info.ip_addr},
                          {"netMask", info.net_mask},
                          {"gateway", info.gateway},
                          {"isDefault", info.is_default},
                          {"metric", info.metric}});
    return result;
}
void WritePrivateFile(const std::string& path, const std::string& data) {
    const std::filesystem::path file(path);
    std::filesystem::create_directories(file.parent_path());
    const auto temporary = path + ".new";
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        throw std::runtime_error("saveFailed");
    bool good     = fchmod(fd, 0600) == 0;
    size_t offset = 0;
    while (good && offset < data.size()) {
        const auto count = write(fd, data.data() + offset, data.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            good = false;
        } else {
            offset += static_cast<size_t>(count);
        }
    }
    good = fsync(fd) == 0 && good;
    close(fd);
    if (!good || rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        throw std::runtime_error("saveFailed");
    }
    const int directory = open(file.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        fsync(directory);
        close(directory);
    }
}
std::string ReadPrivateFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("loadFailed");
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}
nlohmann::json ValidateWifiSettings(const nlohmann::json& input, const nlohmann::json& saved) {
    try {
        const auto ssid = input.at("ssid").get<std::string>();
        if (ssid.empty() || ssid.size() > 32 || ssid.find('\0') != std::string::npos)
            throw std::runtime_error("invalidSsid");
        if (input.value("security", "wpa2") != "wpa2")
            throw std::runtime_error("unsupportedSecurity");
        nlohmann::json result{{"ssid", ssid},
                              {"security", "wpa2"},
                              {"hidden", input.value("hidden", false)},
                              {"dhcp", input.value("dhcp", true)},
                              {"ipAddr", input.value("ipAddr", "")},
                              {"netMask", input.value("netMask", "")},
                              {"gateway", input.value("gateway", "")},
                              {"autoDns", input.value("autoDns", true)},
                              {"dns", input.value("dns", nlohmann::json::array())}};
        if (!result.at("dns").is_array() || result.at("dns").size() > 3)
            throw std::runtime_error("invalidDns");
        for (const auto& dns : result.at("dns"))
            if (!dns.is_string() || !UsableIp(dns.get<std::string>()))
                throw std::runtime_error("invalidDns");
        if ((!result.at("dhcp").get<bool>() && result.at("autoDns").get<bool>()) ||
            (!result.at("autoDns").get<bool>() && result.at("dns").empty()))
            throw std::runtime_error("invalidDns");
        if (!result.at("dhcp").get<bool>()) {
            const auto ip      = result.at("ipAddr").get<std::string>();
            const auto mask    = result.at("netMask").get<std::string>();
            const auto gateway = result.at("gateway").get<std::string>();
            Prefix(mask);
            const auto host = IpValue(ip) & ~IpValue(mask);
            if (!UsableIp(ip) || host == 0 || host == ~IpValue(mask))
                throw std::runtime_error("invalidIpv4");
            if (!gateway.empty()) {
                const auto gateway_host = IpValue(gateway) & ~IpValue(mask);
                if (!UsableIp(gateway) || gateway_host == 0 || gateway_host == ~IpValue(mask) ||
                    (IpValue(ip) & IpValue(mask)) != (IpValue(gateway) & IpValue(mask)) || gateway == ip)
                    throw std::runtime_error("invalidGateway");
            }
        }
        const auto password = input.value("password", "");
        if (password.empty()) {
            if (saved.value("ssid", "") != ssid || !saved.contains("psk"))
                throw std::runtime_error("passwordRequired");
            result["psk"] = saved.at("psk");
        } else if (password.size() == 64 && std::all_of(password.begin(), password.end(),
                                                        [](unsigned char c) { return std::isxdigit(c); })) {
            result["psk"] = password;
        } else {
            if (password.size() < 8 || password.size() > 63 ||
                !std::all_of(password.begin(), password.end(),
                             [](unsigned char c) { return c >= 32 && c <= 126; }))
                throw std::runtime_error("invalidPassword");
            std::array<CryptoPP::byte, 32> key{};
            CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA1> derive;
            derive.DeriveKey(key.data(), key.size(), 0,
                             reinterpret_cast<const CryptoPP::byte*>(password.data()), password.size(),
                             reinterpret_cast<const CryptoPP::byte*>(ssid.data()), ssid.size(), 4096);
            result["psk"] = Hex(std::string(reinterpret_cast<const char*>(key.data()), key.size()));
        }
        return result;
    } catch (const nlohmann::json::exception&) {
        throw std::runtime_error("invalidWifiConfig");
    }
}
std::string WifiSupplicantConfig(const nlohmann::json& settings, bool auto_connect) {
    return "ctrl_interface=/run/wpa_supplicant\nupdate_config=0\nnetwork={\n\tssid=" +
           Hex(settings.at("ssid").get<std::string>()) + "\n\tpsk=" + settings.at("psk").get<std::string>() +
           "\n\tkey_mgmt=WPA-PSK\n\tproto=RSN\n\tscan_ssid=" + (settings.value("hidden", false) ? "1" : "0") +
           "\n\tdisabled=" + (auto_connect ? "0" : "1") + "\n}\n";
}
std::string WifiNetworkConfig(const std::string& interface_name, const nlohmann::json& settings) {
    if (!ValidName(interface_name))
        throw std::runtime_error("invalidInterface");
    const bool dhcp     = settings.at("dhcp");
    const bool auto_dns = settings.at("autoDns");
    std::string result =
        "[Match]\nName=" + interface_name +
        "\n\n[Link]\nRequiredForOnline=no\n\n[Network]\nIPv6AcceptRA=no\nLinkLocalAddressing=no\nDHCP=" +
        (dhcp ? "ipv4" : "no") + "\nDNSDefaultRoute=no\n";
    if (!auto_dns)
        for (const auto& dns : settings.at("dns"))
            result += "DNS=" + dns.get<std::string>() + "\n";
    if (!dhcp) {
        result += "Address=" + settings.at("ipAddr").get<std::string>() + "/" +
                  std::to_string(Prefix(settings.at("netMask").get<std::string>())) + "\n";
        result += "IPv4LLRoute=no\n";
        const auto gateway = settings.at("gateway").get<std::string>();
        if (!gateway.empty())
            result += "\n[Route]\nGateway=" + gateway + "\nMetric=600\n";
    }
    result += "\n[DHCPv4]\nRouteMetric=600\nUseDNS=" + std::string(auto_dns ? "yes" : "no") + "\n";
    return result;
}
nlohmann::json ParseWifiScanResults(const std::string& response) {
    auto result = nlohmann::json::array();
    std::map<std::pair<std::string, std::string>, size_t> seen;
    std::istringstream input(response);
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        std::array<std::string, 5> columns;
        size_t begin = 0;
        bool valid   = true;
        for (size_t i = 0; i < 4; ++i) {
            const auto tab = line.find('\t', begin);
            if (tab == std::string::npos) {
                valid = false;
                break;
            }
            columns[i] = line.substr(begin, tab - begin);
            begin      = tab + 1;
        }
        if (!valid)
            continue;
        columns[4]           = DecodeSsid(line.substr(begin));
        const auto& flags    = columns[3];
        std::string security = "open";
        if (flags.find("WPA2-PSK") != std::string::npos || flags.find("RSN-PSK") != std::string::npos)
            security = "wpa2";
        else if (flags.find("SAE") != std::string::npos)
            security = "wpa3";
        else if (flags.find("EAP") != std::string::npos)
            security = "enterprise";
        else if (flags.find("WPA") != std::string::npos || flags.find("WEP") != std::string::npos)
            security = "unsupported";
        try {
            const int signal = std::stoi(columns[2]);
            auto network     = nlohmann::json{{"ssid", columns[4]},
                                              {"security", security},
                                              {"signal", signal},
                                              {"frequency", std::stoi(columns[1])},
                                              {"bssid", columns[0]}};
            // An arbitrary byte SSID that is not UTF-8 cannot be represented in this JSON API.
            network.dump();
            const auto key   = std::make_pair(columns[4], security);
            const auto found = seen.find(key);
            if (found == seen.end()) {
                seen.emplace(key, result.size());
                result.push_back(std::move(network));
            } else if (signal > result[found->second].at("signal").get<int>()) {
                result[found->second] = std::move(network);
            }
        } catch (const std::exception&) {
            continue;
        }
    }
    return result;
}
std::shared_ptr<IWifiPlatform> MakeWifiPlatform() {
    return std::make_shared<NativeWifiPlatform>();
}
}  // namespace cosmo::platform
