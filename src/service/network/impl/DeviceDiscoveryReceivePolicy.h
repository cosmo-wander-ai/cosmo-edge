// Receive error policy for multicast device discovery.
#pragma once

#include <string_view>
#include <utility>
#include <vector>

#include "platform/WifiPlatform.h"

namespace cosmo::service::detail {

enum class MulticastReceiveAction {
    kRetry,
    kRestartSocket,
};

MulticastReceiveAction ClassifyMulticastReceiveError(int error_code);

std::vector<std::pair<std::string, std::string>> DiscoveryMembershipAddresses(
    const std::vector<platform::NetworkInterfaceState>& interfaces);

bool IsProductionDiscoveryCommandAllowed(std::string_view command);

bool IsProductionDiscoveryCommandAuthenticationRequired(std::string_view command);

}  // namespace cosmo::service::detail
