#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cosmo::service {

// A task configuration is immutable for the lifetime of a run. Call Invalidate
// before stopping or replacing it; publication must use CommitIfCurrent so a
// result cannot cross the configuration/stop boundary after Decide returns.
class VisualDecisionRun {
public:
    VisualDecisionRun(std::string task, std::string epoch, std::string revision)
        : taskId(std::move(task)), runEpoch(std::move(epoch)), configRevision(std::move(revision)) {}
    void Invalidate() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
    }
    bool Active() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }
    // Keep commit short; it must not call back into this run or perform inference.
    bool CommitIfCurrent(const std::function<void()>& commit) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_)
            return false;
        commit();
        return true;
    }
    const std::string taskId;
    const std::string runEpoch;
    const std::string configRevision;

private:
    mutable std::mutex mutex_;
    bool active_{true};
};

// Produced by configuration preparation, never from a worker's answer. Labels
// and temperature are copied from the hash-verified compiled question pack.
struct VisualQuestionRef {
    std::string itemId;
    std::string questionId;
    int questionVersion{0};
    std::string compiledSha256;
    int qtype{-1};
    std::vector<std::string> orderedOptions;
    nlohmann::json temperature;
};

struct VisualDecisionImage {
    std::vector<uint8_t> jpeg;
    int width{0};
    int height{0};
};

struct VisualDecisionRequest {
    std::string frameId;
    std::string roiId;
    std::vector<VisualQuestionRef> questions;
};

struct VisualDecisionResult {
    // The envelope retains trusted request identity even on rejection/timeout.
    // No image bytes or prompts are copied into the audit record.
    nlohmann::json request;
    nlohmann::json response;
    bool AllCompleted() const {
        return response.value("status", std::string()) == "completed";
    }
};

// Shared by alarm review and ROI flow judgment. This is a typed image decision
// API, not a text-generation adapter. Numerical completion never grants business
// qualification or permission to suppress an alarm.
class IVisualDecisionService {
public:
    using Prepare                     = std::function<VisualDecisionImage()>;
    virtual ~IVisualDecisionService() = default;
    virtual bool Available() const    = 0;
    // Thread-safe; reserves capacity before preparing the crop. Queueing, JPEG
    // preparation, IPC and all questions share one absolute deadline.
    virtual VisualDecisionResult Decide(
        const VisualDecisionRequest& request, std::shared_ptr<VisualDecisionRun> run, Prepare prepare,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1500)) = 0;
    virtual nlohmann::json Counters() const                                  = 0;
};

}  // namespace cosmo::service
