/// @file INetworkService.h
/// @brief Aggregate network service interface — inherits all ISP sub-interfaces
///        (IHttpLifecycle, IMqttLifecycle, INetworkConfig).
///        Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include <string>

#include "service/detail/ServiceRegistry.h"
#include "service/network/IHttpLifecycle.h"
#include "service/network/IMqttLifecycle.h"
#include "service/network/INetworkConfig.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Aggregate network service inheriting the narrow lifecycle and config
/// sub-interfaces.
class INetworkService : public IHttpLifecycle, public IMqttLifecycle, public INetworkConfig {
public:
    virtual ~INetworkService() = default;
};

}  // namespace cosmo::service
