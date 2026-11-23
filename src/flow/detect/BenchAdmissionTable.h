#pragma once
#include <map>
#include <memory>
#include <string>
namespace cosmo {
// Caller must hold the owning detector's native_registration_mutex_.
// The admission's own gate remains authoritative for in-flight work.
template <class Admission>
class BenchAdmissionTable {
    std::map<std::string, std::shared_ptr<const Admission>> routes_;

public:
    bool Insert(const std::string& channel, std::shared_ptr<const Admission> route) {
        return !channel.empty() && route && routes_.emplace(channel, std::move(route)).second;
    }
    std::shared_ptr<const Admission> Find(const std::string& channel) const {
        auto it = routes_.find(channel);
        return it == routes_.end() ? nullptr : it->second;
    }
    bool EraseIf(const std::string& channel, const std::shared_ptr<const Admission>& expected) {
        auto it = routes_.find(channel);
        if (it == routes_.end() || !expected || it->second != expected)
            return false;
        routes_.erase(it);
        return true;
    }
    std::size_t Size() const noexcept {
        return routes_.size();
    }
};
}  // namespace cosmo
