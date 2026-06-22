// Global thread registry for diagnostics (register/unregister running threads).

#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace cosmo::util {

class ThreadRegistry {
public:
    ThreadRegistry()  = default;
    ~ThreadRegistry() = default;

    void AddThread(const std::string& uuid, const std::string& name);
    bool RemoveThread(const std::string& uuid);
    void ShowAll() const;

private:
    std::unordered_map<std::string, std::string> thread_list_;  // uuid -> name
    mutable std::shared_mutex mtx_;
};

// Free functions backed by the process-global registry.
void RegisterThread(const std::string& uuid, const std::string& name);
bool UnregisterThread(const std::string& uuid);
void ShowAllThreads();

}  // namespace cosmo::util
