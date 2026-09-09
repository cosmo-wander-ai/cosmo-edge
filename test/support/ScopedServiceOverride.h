#pragma once

#include <exception>
#include <stdexcept>
#include <string>
#include <typeinfo>

#include "service/detail/ServiceRegistry.h"

namespace cosmo::test {

/// Registers one non-owning test service for the lifetime of this guard.
///
/// Tests must declare one guard for every interface they actually expose. The
/// interface must be unregistered on entry so a test cannot silently replace
/// state left by another test.
template <typename Interface>
class ScopedServiceOverride final {
public:
    explicit ScopedServiceOverride(Interface& service) : registry_(service::ServiceRegistry::Instance()) {
        if (registry_.Has<Interface>()) {
            throw std::logic_error(std::string("test service is already registered: ") +
                                   typeid(Interface).name());
        }
        registry_.Set<Interface>(&service);
    }

    ~ScopedServiceOverride() noexcept {
        try {
            registry_.Set<Interface>(nullptr);
        } catch (...) {
            std::terminate();
        }
    }

    ScopedServiceOverride(const ScopedServiceOverride&)            = delete;
    ScopedServiceOverride& operator=(const ScopedServiceOverride&) = delete;
    ScopedServiceOverride(ScopedServiceOverride&&)                 = delete;
    ScopedServiceOverride& operator=(ScopedServiceOverride&&)      = delete;

private:
    service::ServiceRegistry& registry_;
};

}  // namespace cosmo::test
