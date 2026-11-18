// Receive error policy for multicast device discovery.

#include "service/network/impl/DeviceDiscoveryReceivePolicy.h"

#include <arpa/inet.h>

#include <cerrno>

namespace cosmo::service::detail {

MulticastReceiveAction ClassifyMulticastReceiveError(int error_code) {
    if (error_code == EAGAIN || error_code == EINTR) {
        return MulticastReceiveAction::kRetry;
    }
#if EWOULDBLOCK != EAGAIN
    if (error_code == EWOULDBLOCK) {
        return MulticastReceiveAction::kRetry;
    }
#endif
    return MulticastReceiveAction::kRestartSocket;
}

std::vector<std::pair<std::string, std::string>> DiscoveryMembershipAddresses(
    const std::vector<platform::NetworkInterfaceState>& interfaces) {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& interface : interfaces) {
        in_addr address{};
        if (!interface.link_up || interface.interface_name.empty() || interface.interface_name == "lo" ||
            inet_pton(AF_INET, interface.ip_addr.c_str(), &address) != 1) {
            continue;
        }
        const auto host_address = ntohl(address.s_addr);
        if (host_address == INADDR_ANY || host_address == INADDR_BROADCAST ||
            (host_address >> 24) == IN_LOOPBACKNET || IN_MULTICAST(host_address)) {
            continue;
        }
        result.emplace_back(interface.interface_name, interface.ip_addr);
    }
    return result;
}

bool IsProductionDiscoveryCommandAllowed(std::string_view command) {
    return command == "probe";
}

bool IsProductionDiscoveryCommandAuthenticationRequired(std::string_view command) {
    return command == "modifyNetCard" || command == "writeHWInfo" || command == "modifyAuthCode" ||
           command == "queryAuthMessage";
}

}  // namespace cosmo::service::detail
