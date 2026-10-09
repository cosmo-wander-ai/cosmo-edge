#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace cosmo::service {

// Shared across copied frames and associated alarms. Last-owner destruction
// closes a decision that did not reach a terminal publisher. Alarm links in the
// database take precedence over this local delivery hint.
class VisualAuditLease {
public:
    explicit VisualAuditLease(std::function<void(const std::string&)> close) : close_(std::move(close)) {}
    ~VisualAuditLease() {
        Seal("no_alarm");
    }
    void Seal(const std::string& status) noexcept {
        if (closed_.exchange(true))
            return;
        try {
            close_(status);
        } catch (...) {
        }
    }

private:
    std::function<void(const std::string&)> close_;
    std::atomic<bool> closed_{false};
};

struct VisualAuditReceipt {
    nlohmann::json metadata{{"schema", 1}, {"begin", "not_configured"}, {"finish", "not_attempted"}};
    std::shared_ptr<VisualAuditLease> lease;
    bool Begun() const {
        return metadata.value("begin", std::string()) == "stored";
    }
};

class IVisualAuditService {
public:
    virtual ~IVisualAuditService()                                                              = default;
    virtual VisualAuditReceipt Begin(const nlohmann::json& request,
                                     std::chrono::milliseconds waitBudget = std::chrono::milliseconds{
                                         250}) noexcept                                         = 0;
    virtual bool Complete(VisualAuditReceipt& receipt, const nlohmann::json& response) noexcept = 0;
    virtual nlohmann::json Page(const std::string& eventId, const std::string& requestId, int page,
                                int size)                                                       = 0;
    virtual nlohmann::json Status() const                                                       = 0;
};

}  // namespace cosmo::service
