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
    size_t maxConfigurations{64};
    size_t maxInputBytes{8 * 1024 * 1024};
    unsigned maxAttemptsPerBatch{3};
    std::chrono::milliseconds retryDelay{250};
    bool Valid() const;
    static VisualQuestionCompilerOptions FromEnvironment();
};

class VisualQuestionServiceImpl final : public IVisualQuestionService {
public:
    using Executor       = std::function<util::BoundedProcessResult(
        const std::vector<std::string>&, const std::string&, std::chrono::steady_clock::time_point,
        const std::function<bool()>&)>;
    using NativeCompiler = std::function<VisualQuestionPreparation(
        const std::vector<VisualQuestionSpec>&, const std::shared_ptr<VisualDecisionRun>&)>;
    explicit VisualQuestionServiceImpl(
        VisualQuestionCompilerOptions options = VisualQuestionCompilerOptions::FromEnvironment(),
        Executor executor = {}, NativeCompiler nativeCompiler = {});
    ~VisualQuestionServiceImpl() override;
    std::shared_future<VisualQuestionPreparation> Prepare(std::vector<VisualQuestionSpec> questions,
                                                          std::shared_ptr<VisualDecisionRun> run,
                                                          std::chrono::milliseconds timeout) override;
    void Stop();
    nlohmann::json Counters() const override;

private:
    struct Job {
        std::vector<VisualQuestionSpec> questions;
        std::shared_ptr<VisualDecisionRun> run;
        std::string input;
        std::string inputSha256;
        std::chrono::steady_clock::time_point deadline;
        std::chrono::steady_clock::time_point eligible;
        VisualQuestionPreparation combined;
        std::atomic<size_t> offset{0};
        std::atomic<unsigned> attempts{0};
        unsigned batchAttempts{0};
        std::promise<VisualQuestionPreparation> promise;
    };
    VisualQuestionPreparation Compile(const Job& job, bool& retryable);
    std::unique_ptr<Job> Batch(const Job& job) const;
    void SweepLocked();
    void SettleLocked(const std::shared_ptr<Job>& job, VisualQuestionPreparation result);
    void Work();
    void Expire();
    const VisualQuestionCompilerOptions options_;
    const Executor executor_;
    const NativeCompiler nativeCompiler_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::shared_ptr<Job>> queue_;
    size_t outstanding_{0};
    size_t inputBytes_{0};
    std::shared_ptr<Job> active_;
    uint64_t accepted_{0}, completed_{0}, failed_{0}, retried_{0};
    std::atomic<uint64_t> rejected_{0};
    std::atomic<bool> stopped_{false};
    std::once_flag stopOnce_;
    std::thread worker_;
    std::thread expiry_;
};

}  // namespace cosmo::service
