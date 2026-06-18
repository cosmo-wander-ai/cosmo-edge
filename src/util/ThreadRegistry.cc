// Global thread registry for diagnostics.

#include "util/ThreadRegistry.h"

#include "util/Log.h"

namespace cosmo::util {

namespace {
    // Meyer's Singleton ensures safe initialization order and destruction.
    ThreadRegistry& GetGlobalRegistry() {
        static ThreadRegistry instance;
        return instance;
    }
}  // namespace

void ThreadRegistry::AddThread(const std::string& uuid, const std::string& name) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    thread_list_[uuid] = name;
}

bool ThreadRegistry::RemoveThread(const std::string& uuid) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    return thread_list_.erase(uuid) > 0;
}

void ThreadRegistry::ShowAll() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& pair : thread_list_) {
        LOG_INFO("Thread [{}]", pair.second);
    }
    LOG_INFO("Thread Total Count is {}", thread_list_.size());
}

void RegisterThread(const std::string& uuid, const std::string& name) {
    GetGlobalRegistry().AddThread(uuid, name);
}

bool UnregisterThread(const std::string& uuid) {
    return GetGlobalRegistry().RemoveThread(uuid);
}

void ShowAllThreads() {
    GetGlobalRegistry().ShowAll();
}

}  // namespace cosmo::util
