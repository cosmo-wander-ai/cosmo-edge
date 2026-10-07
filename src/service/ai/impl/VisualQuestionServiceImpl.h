#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>

#include "service/ai/IVisualQuestionService.h"
#include "util/BoundedProcess.h"

namespace cosmo::service {

struct VisualQuestionCompilerOptions {
    std::string python{"/usr/bin/python3"};
    std::string script;
    std::string manifestPath;
    std::string manifestSha256;
    std::string cacheDirectory{"/data/cosmo-laya/question-cache"};
    nlohmann::json binding;
    bool Valid() const;
    static VisualQuestionCompilerOptions FromEnvironment();
};

class VisualQuestionServiceImpl final : public IVisualQuestionService {
public:
    using Executor = std::function<util::BoundedProcessResult(
        const std::vector<std::string>&, const std::string&, std::chrono::steady_clock::time_point,
        const std::function<bool()>&)>;
    explicit VisualQuestionServiceImpl(
        VisualQuestionCompilerOptions options = VisualQuestionCompilerOptions::FromEnvironment(),
        Executor executor                     = {});
    ~VisualQuestionServiceImpl() override;
    std::shared_future<VisualQuestionPreparation> Prepare(std::vector<VisualQuestionSpec> questions,
                                                          std::shared_ptr<VisualDecisionRun> run,
                                                          std::chrono::milliseconds timeout) override;
    void Stop();

private:
    struct Job {
        std::vector<VisualQuestionSpec> questions;
        std::shared_ptr<VisualDecisionRun> run;
        std::string input;
        std::string inputSha256;
        std::chrono::steady_clock::time_point deadline;
        std::promise<VisualQuestionPreparation> promise;
    };
    VisualQuestionPreparation Compile(const Job& job);
    void Work();
    const VisualQuestionCompilerOptions options_;
    const Executor executor_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::shared_ptr<Job>> queue_;
    size_t outstanding_{0};
    std::atomic<bool> stopped_{false};
    std::once_flag stopOnce_;
    std::thread worker_;
};

}  // namespace cosmo::service
