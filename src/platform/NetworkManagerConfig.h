// Internal NetworkManager workflow. The runner executes argv without a shell.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cosmo::platform {
struct NetCardInfo;
}
namespace cosmo::platform::internal {
bool ApplyNetworkManagerCard(const NetCardInfo& info);
class NetworkManagerConfig {
public:
    using Runner = std::function<int(const std::vector<std::string>&, std::string&)>;
    explicit NetworkManagerConfig(Runner runner) : runner_(std::move(runner)) {}

    // NetCardOp validates inputs before changing persistent settings.
    bool Apply(const std::string& interface, bool dhcp, const std::string& address,
               const std::string& gateway, const std::string& dns, bool main) {
        std::string uuid;
        if (!FindProfile(interface, uuid))
            return false;
        if (!Run({"connection", "modify", "uuid", uuid, "connection.autoconnect", "yes", "ipv4.method",
                  dhcp ? "auto" : "manual", "ipv4.addresses", dhcp ? "" : address, "ipv4.gateway",
                  dhcp ? "" : gateway, "ipv4.dns", dns, "ipv4.ignore-auto-dns", dns.empty() ? "no" : "yes",
                  "ipv4.never-default", main ? "no" : "yes"}))
            return false;
        return Activate(interface, uuid);
    }
    bool SetDns(const std::string& interface, const std::string& dns) {
        std::string uuid;
        if (!ActiveProfile(interface, uuid) || uuid.empty())
            return false;
        if (!Run({"connection", "modify", "uuid", uuid, "ipv4.dns", dns, "ipv4.ignore-auto-dns",
                  dns.empty() ? "no" : "yes"}))
            return false;
        return Activate(interface, uuid);
    }
    bool IsDhcp(const std::string& interface) {
        std::string uuid;
        return ActiveProfile(interface, uuid) && !uuid.empty() &&
               Run({"-g", "ipv4.method", "connection", "show", "uuid", uuid}) && output_ == "auto";
    }
    const std::string& Error() const {
        return output_;
    }

private:
    bool Run(const std::vector<std::string>& arguments) {
        std::vector<std::string> argv{"nmcli", "--wait", "15"};
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        output_.clear();
        const auto result = runner_(argv, output_);
        const auto end    = output_.find_last_not_of("\r\n");
        output_.erase(end == std::string::npos ? 0 : end + 1);
        return result == 0;
    }
    bool ActiveProfile(const std::string& interface, std::string& uuid) {
        if (!Run({"-g", "GENERAL.CON-UUID", "device", "show", interface}))
            return false;
        uuid = output_ == "--" ? "" : output_;
        return true;
    }
    bool FindProfile(const std::string& interface, std::string& uuid) {
        if (!ActiveProfile(interface, uuid))
            return false;
        if (!uuid.empty())
            return true;
        const auto name = "cosmo-" + interface;
        if (!Run({"-g", "connection.uuid", "connection", "show", "id", name})) {
            // Do not activate a DHCP profile before setting the desired IP.
            if (!Run({"connection", "add", "type", "ethernet", "ifname", interface, "con-name", name,
                      "connection.autoconnect", "no"}) ||
                !Run({"-g", "connection.uuid", "connection", "show", "id", name}))
                return false;
        }
        uuid = output_;
        return !uuid.empty();
    }
    bool Activate(const std::string& interface, const std::string& uuid) {
        std::string active;
        if (ActiveProfile(interface, active) && active == uuid && Run({"device", "reapply", interface}))
            return true;
        return Run({"connection", "up", "uuid", uuid, "ifname", interface});
    }
    Runner runner_;
    std::string output_;
};
}  // namespace cosmo::platform::internal
