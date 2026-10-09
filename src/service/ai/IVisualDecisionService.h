#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "service/ai/IVisualAuditService.h"

namespace cosmo::service {

// A task configuration is immutable for the lifetime of a run. Call Invalidate
// before stopping or replacing it; publication must use CommitIfCurrent so a
// result cannot cross the configuration/stop boundary after Decide returns.
class VisualDecisionRun {
public:
    VisualDecisionRun(std::string task, std::string epoch, std::string revision, std::string model = "")
        : taskId(std::move(task)),
          runEpoch(std::move(epoch)),
          configRevision(std::move(revision)),
          atomicCode(std::move(model)) {}
    ~VisualDecisionRun() {
        if (releaseModel_)
            releaseModel_();
    }
    bool AcquireModel(const std::function<void()>& acquire, std::function<void()> release) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_)
            return false;
        if (!releaseModel_) {
            acquire();
            releaseModel_ = std::move(release);
        }
        return true;
    }
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
    const std::string atomicCode;

private:
    mutable std::mutex mutex_;
    bool active_{true};
    std::function<void()> releaseModel_;
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
    // Native model-bound preparation; not persisted in alarm/audit records.
    nlohmann::json nativeSpec;
    std::string modelIdentity;
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
    std::string mode{"review"};
    std::string policyId;
    std::string qualificationRevision;
};

struct VisualDecisionResult {
    // The envelope retains trusted request identity even on rejection/timeout.
    // No image bytes or prompts are copied into the audit record.
    nlohmann::json request;
    nlohmann::json response;
    VisualAuditReceipt audit;
    bool AllCompleted() const {
        return response.value("status", std::string()) == "completed";
    }
    bool Retain() const {
        const auto decision = response.find("decision");
        return decision == response.end() || !decision->value("business_qualified", false) ||
               !decision->value("filter_applied", false) || decision->value("retain", true);
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
    // preparation and all questions share one absolute deadline.
    virtual VisualDecisionResult Decide(
        const VisualDecisionRequest& request, std::shared_ptr<VisualDecisionRun> run, Prepare prepare,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1500)) = 0;
    virtual nlohmann::json Counters() const                                  = 0;
};

}  // namespace cosmo::service
