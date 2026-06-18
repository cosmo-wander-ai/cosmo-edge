#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "util/Log.h"

namespace cosmo::service {

/// Lightweight DI container with compile-time type safety.
///
/// Supports two registration modes:
///   1. Owning:     Registry takes ownership via unique_ptr (production).
///   2. Non-owning: Registry stores a raw pointer (tests / external ownership).
///
/// Usage:
///   // Owning registration (production)
///   registry.Register<IFooService>(std::make_unique<FooServiceImpl>());
///
///   // Non-owning registration (tests)
///   MockFooService mock;
///   registry.Set<IFooService>(&mock);
///
///   // Retrieval (same for both modes)
///   auto& foo = registry.Get<IFooService>();
///
/// Thread safety: Get() uses shared_lock; Set/Register use unique_lock.
/// In practice, all registration happens in SwDeviceInit() (single-threaded).
class ServiceRegistry {
public:
    static ServiceRegistry& Instance();

    /// Register an owned service implementation. The registry takes ownership
    /// of the unique_ptr and will destroy it in reverse registration order
    /// when ShutdownAll() is called.
    template <typename Interface, typename Impl>
    void Register(std::unique_ptr<Impl> impl) {
        auto* raw = impl.get();
        // Convert unique_ptr to shared_ptr<void> with proper typed deleter.
        // shared_ptr<void> is copy-constructible and handles type-erased destruction.
        std::shared_ptr<void> handle(impl.release(), [](void* p) { delete static_cast<Impl*>(p); });
        std::unique_lock lock(mutex_);
        auto key      = std::type_index(typeid(Interface));
        entries_[key] = Entry{raw, nullptr};
        owned_.push_back(OwnedEntry{key, std::move(handle)});
    }

    /// Register a non-owning pointer (for tests or externally-managed objects).
    /// Pass nullptr to unregister.
    template <typename Interface>
    void Set(Interface* impl) {
        std::unique_lock lock(mutex_);
        auto key = std::type_index(typeid(Interface));
        if (impl) {
            entries_[key] = Entry{static_cast<void*>(impl), nullptr};
        } else {
            entries_.erase(key);
        }
    }

    /// Retrieve a service reference. Aborts if not registered (programming error).
    template <typename Interface>
    Interface& Get() {
        std::shared_lock lock(mutex_);
        auto key = std::type_index(typeid(Interface));
        auto it  = entries_.find(key);
        if (it == entries_.end() || it->second.ptr == nullptr) {
            LOG_ERRO("FATAL: {} not registered in ServiceRegistry!", typeid(Interface).name());
            std::abort();
        }
        return *static_cast<Interface*>(it->second.ptr);
    }

    /// Check if a service is registered.
    template <typename Interface>
    bool Has() const {
        std::shared_lock lock(mutex_);
        auto key = std::type_index(typeid(Interface));
        auto it  = entries_.find(key);
        return it != entries_.end() && it->second.ptr != nullptr;
    }

    /// Destroy all owned services in reverse registration order, then clear
    /// all entries. Call this in SwDeviceDestroy().
    void ShutdownAll();

    /// Clear all entries without destroying owned services.
    /// Useful in tests where mocks are stack-allocated.

    /// Number of registered services (for diagnostics).
    size_t Size() const {
        std::shared_lock lock(mutex_);
        return entries_.size();
    }

private:
    ServiceRegistry()                                  = default;
    ~ServiceRegistry()                                 = default;
    ServiceRegistry(const ServiceRegistry&)            = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    struct Entry {
        void* ptr      = nullptr;
        void* reserved = nullptr;  // For future use (e.g., metadata)
    };

    struct OwnedEntry {
        std::type_index key;
        std::shared_ptr<void> handle;  // Type-erased destructor via shared_ptr deleter
    };

    std::unordered_map<std::type_index, Entry> entries_;
    std::vector<OwnedEntry> owned_;  // Maintains registration order for LIFO destruction
    mutable std::shared_mutex mutex_;
};

}  // namespace cosmo::service
