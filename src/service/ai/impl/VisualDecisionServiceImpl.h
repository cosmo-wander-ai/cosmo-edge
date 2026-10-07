#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <thread>

#include "service/ai/IVisualDecisionService.h"
#include "service/ai/impl/VisualDecisionPolicy.h"
#include "service/ai/impl/VisualDecisionProtocol.h"

namespace cosmo::service {

struct VisualDecisionOptions {
    visual::Release release;
    std::string socketPath{"/run/cosmo-visual-decision/worker.sock"};
    size_t capacity{3};
    size_t perTaskLimit{2};
    std::string unavailableReason{"release_not_configured"};
    nlohmann::json acceptance = nlohmann::json::object();
    std::string acceptanceReason{"not_configured"};
    // Installed service configuration pins both the manifest location and SHA.
    // No model is loaded and no old VLM is invoked when configuration is absent.
    static VisualDecisionOptions FromEnvironment();
};

class VisualDecisionServiceImpl final : public IVisualDecisionService {
public:
    using Transport = std::function<visual::Json(const std::string&, const visual::Json&,
                                                 const std::vector<uint8_t>&, visual::Clock::time_point)>;
    explicit VisualDecisionServiceImpl(
        VisualDecisionOptions options = VisualDecisionOptions::FromEnvironment(), Transport transport = {},
        IVisualAuditService* audit = nullptr);
    ~VisualDecisionServiceImpl() override;
    bool Available() const override;
    VisualDecisionResult Decide(const VisualDecisionRequest& request, std::shared_ptr<VisualDecisionRun> run,
                                Prepare prepare, std::chrono::milliseconds timeout) override;
    nlohmann::json Counters() const override;
    void Stop();

private:
    struct Job {
        visual::Json identity;
        std::vector<VisualQuestionRef> questions;
        std::shared_ptr<VisualDecisionRun> run;
        Prepare prepare;
        visual::Clock::time_point deadline;
        std::promise<VisualDecisionResult> promise;
        std::atomic<bool> finished{false};
    };
    void Work();
    void Finish(const std::shared_ptr<Job>& job, VisualDecisionResult result);
    const VisualDecisionOptions options_;
    const Transport transport_;
    IVisualAuditService* const audit_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::map<std::string, size_t> perTask_;
    size_t outstanding_{0};
    std::string lastTask_;
    bool stopped_{false};
    std::once_flag stopOnce_;
    std::thread worker_;
    std::atomic<uint64_t> accepted_{0};
    std::atomic<uint64_t> rejected_{0};
    std::atomic<uint64_t> completed_{0};
    std::atomic<uint64_t> partial_{0};
    std::atomic<uint64_t> unknown_{0};
};

}  // namespace cosmo::service
