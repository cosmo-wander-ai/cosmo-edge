// A process-wide lease serializes network writes, including async confirmation.
#pragma once

#include <memory>

namespace cosmo::service {
class NetworkMutationLease final {
    struct AcquiredTag {};

public:
    explicit NetworkMutationLease(AcquiredTag);
    static std::shared_ptr<NetworkMutationLease> TryAcquire();
    static bool IsBusy();
    ~NetworkMutationLease();
    NetworkMutationLease(const NetworkMutationLease&)            = delete;
    NetworkMutationLease& operator=(const NetworkMutationLease&) = delete;
};
}  // namespace cosmo::service
