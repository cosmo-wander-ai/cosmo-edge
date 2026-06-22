// ServiceRegistry — Service Registry implementation.

#include "service/detail/ServiceRegistry.h"

namespace cosmo::service {

ServiceRegistry& ServiceRegistry::Instance() {
    static ServiceRegistry instance;
    return instance;
}

void ServiceRegistry::ShutdownAll() {
    std::unique_lock lock(mutex_);
    // Clear all lookup entries first (prevent dangling Get() calls)
    entries_.clear();
    // Destroy owned services in reverse registration order (LIFO).
    // Popping from the back ensures reverse-order destruction.
    while (!owned_.empty()) {
        owned_.pop_back();  // shared_ptr destructor invokes the typed deleter
    }
}

}  // namespace cosmo::service
