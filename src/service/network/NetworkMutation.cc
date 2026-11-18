#include "service/network/NetworkMutation.h"

#include <atomic>

namespace cosmo::service {
namespace {
    std::atomic<bool> network_busy{false};
}
std::shared_ptr<NetworkMutationLease> NetworkMutationLease::TryAcquire() {
    bool expected = false;
    if (!network_busy.compare_exchange_strong(expected, true))
        return {};
    try {
        return std::make_shared<NetworkMutationLease>(AcquiredTag{});
    } catch (...) {
        network_busy.store(false);
        throw;
    }
}
NetworkMutationLease::NetworkMutationLease(AcquiredTag) {}
bool NetworkMutationLease::IsBusy() {
    return network_busy.load();
}
NetworkMutationLease::~NetworkMutationLease() {
    network_busy.store(false);
}
}  // namespace cosmo::service
