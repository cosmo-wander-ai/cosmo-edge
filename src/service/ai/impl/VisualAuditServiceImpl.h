#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "service/ai/IVisualAuditService.h"

namespace cosmo::service {

class VisualAuditServiceImpl final : public IVisualAuditService {
public:
    // Existing alarm database only. Uses an independent connection with short
    // lock waits so the main connection's five-second busy timeout is not
    // inherited by inference. Schema/recovery must succeed before admission.
    explicit VisualAuditServiceImpl(const std::string& databasePath);
    ~VisualAuditServiceImpl() override;
    VisualAuditReceipt Begin(const nlohmann::json& request,
                             std::chrono::milliseconds waitBudget = std::chrono::milliseconds{
                                 250}) noexcept override;
    bool Complete(VisualAuditReceipt& receipt, const nlohmann::json& response) noexcept override;
    nlohmann::json Page(const std::string& eventId, const std::string& requestId, int page,
                        int size) override;
    nlohmann::json Status() const override;
    void Maintain();

private:
    struct State;
    std::shared_ptr<State> state_;
    std::mutex maintenanceMutex_;
    std::condition_variable maintenanceCv_;
    bool stopped_{false};
    std::thread maintenance_;
};

}  // namespace cosmo::service
