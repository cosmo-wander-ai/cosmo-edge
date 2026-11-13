// Authenticated HTTP-only Wi-Fi dispatch, with credential-safe parse errors.
#pragma once

#include <string>
#include <system_error>

#include "service/network/IWifiConfig.h"
#include "util/IRequestDispatcher.h"

namespace cosmo {
class MessageWifiHandler {
public:
    explicit MessageWifiHandler(service::IWifiConfig& wifi);
    std::string Handle(const std::string& action, const RequestDispatchContext& context,
                       const std::string& body, std::error_condition& error);

private:
    service::IWifiConfig& wifi_;
};
}  // namespace cosmo
