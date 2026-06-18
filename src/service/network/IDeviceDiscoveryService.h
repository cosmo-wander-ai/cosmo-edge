// Device discovery service interface — multicast-based device search,
// network card config, HW info write and auth code management.
#pragma once

#include "service/detail/ServiceRegistry.h"

namespace cosmo::service {

class IDeviceDiscoveryService {
public:
    virtual ~IDeviceDiscoveryService() = default;

    /// Start the discovery service asynchronously.
    /// Internally initializes multicast with retry and starts recv loop.
    virtual void Start() = 0;

    /// Stop listening, release socket and join all threads.
    virtual void Stop() = 0;
};

}  // namespace cosmo::service
