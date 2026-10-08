#include "service/ai/impl/VisualDecisionServiceImpl.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <stdexcept>

#include "util/UuidUtil.h"

namespace cosmo::service {
namespace {
    using visual::Clock;
    using visual::Json;

    std::string Environment(const char* name) {
        const char* value = std::getenv(name);
        return value ? value : "";
    }

    VisualDecisionResult Unknown(const Json& identity, const std::string& reason) {
        return {identity, visual::Failure(identity, reason)};
    }
}  // namespace

VisualDecisionOptions VisualDecisionOptions::FromEnvironment() {
    VisualDecisionOptions options;
    const auto path = Environment("COSMO_VISUAL_MANIFEST");
    const auto sha  = Environment("COSMO_VISUAL_MANIFEST_SHA256");
    if (path.empty() && sha.empty())
        return options;
    try {
        options.release   = visual::ReadRelease(path, sha);
        const auto socket = Environment("COSMO_VISUAL_SOCKET");
        if (!socket.empty())
            options.socketPath = socket;
    } catch (...) {
        options.unavailableReason = "release_identity_invalid";
    }
    if (options.release.Valid()) {
        try {
            options.acceptance       = visual::ReadAcceptance(Environment("COSMO_VISUAL_ACCEPTANCE"),
                                                              Environment("COSMO_VISUAL_ACCEPTANCE_SHA256"),
                                                              options.release.manifestSha256);
            options.acceptanceReason = options.acceptance.empty() ? "not_configured" : "ready";
        } catch (...) {
            options.acceptanceReason = "acceptance_identity_invalid";
        }
    }
    return options;
}

VisualDecisionServiceImpl::VisualDecisionServiceImpl(VisualDecisionOptions options, Transport transport,
                                                     IVisualAuditService* audit,
                                                     NativeInference nativeInference)
    : options_(std::move(options)),
      transport_(transport ? std::move(transport) : visual::Exchange),
      nativeInference_(std::move(nativeInference)),
      audit_(audit) {
    if (options_.capacity == 0 || options_.capacity > 32 || options_.perTaskLimit == 0 ||
        options_.perTaskLimit > options_.capacity)
        throw std::invalid_argument("invalid_visual_queue_limits");
    if (nativeInference_ || options_.release.Valid())
        worker_ = std::thread(&VisualDecisionServiceImpl::Work, this);
}

VisualDecisionServiceImpl::~VisualDecisionServiceImpl() {
    Stop();
}

bool VisualDecisionServiceImpl::Available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (nativeInference_ || options_.release.Valid()) && !stopped_ &&
           (!audit_ || audit_->Status().value("available", false));
}

VisualDecisionResult VisualDecisionServiceImpl::Decide(const VisualDecisionRequest& request,
                                                       std::shared_ptr<VisualDecisionRun> run,
                                                       Prepare prepare, std::chrono::milliseconds timeout) {
    const auto started = Clock::now();
    auto job           = std::make_shared<Job>();
    job->deadline =
        started + std::min(std::chrono::milliseconds(30000), std::max(std::chrono::milliseconds(0), timeout));
    job->run       = std::move(run);
    job->prepare   = std::move(prepare);
    job->questions = request.questions;
    job->identity  = {
        {"protocol", visual::kProtocol},
        {"profile", visual::kProfile},
        {"manifest_sha256",
         options_.release.manifestSha256.empty() ? Json(nullptr) : Json(options_.release.manifestSha256)},
        {"request_id", cosmo::util::GenerateUUID()},
        {"frame_id", request.frameId},
        {"roi_id", request.roiId},
        {"task_id", job->run ? job->run->taskId : ""},
        {"run_epoch", job->run ? job->run->runEpoch : ""},
        {"config_revision", job->run ? job->run->configRevision : ""},
        {"items", Json::array()}};
    if (nativeInference_) {
        job->identity["atomic_code"] = job->run ? job->run->atomicCode : "";
        if (!request.questions.empty())
            job->identity["manifest_sha256"] = request.questions.front().modelIdentity;
    }
    for (const auto& question : request.questions)
        job->identity["items"].push_back(visual::ItemIdentity(question));
    // Admission competes with fully synchronous alarm writes on the same
    // device. Use the caller's remaining time rather than failing every
    // request after a fixed 250 ms; short requests retain their own deadline.
    auto auditBudget = std::chrono::milliseconds{250};
    if (timeout.count() > 0 && timeout.count() <= 30000)
        auditBudget =
            std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(job->deadline - Clock::now()),
                       std::chrono::milliseconds{0}, std::chrono::milliseconds{1000});
    auto receipt = audit_ ? audit_->Begin(job->identity, auditBudget) : VisualAuditReceipt{};
    auto audited = [&](VisualDecisionResult result) {
        // Persist the caller-visible deadline/cancellation outcome, not a late
        // worker completion. No database work runs under the queue mutex.
        result.response["decision"] = visual::EvaluateDecision(
            options_.acceptance, request, job->run ? job->run->taskId : "", result.response);
        if (audit_ && !audit_->Complete(receipt, result.response)) {
            result.response["decision"].update(
                {{"retain", true}, {"filter_applied", false}, {"reason", "audit_result_write_failed"}});
        }
        if (!audit_ && request.mode == "filter") {
            result.response["decision"].update(
                {{"retain", true}, {"filter_applied", false}, {"reason", "audit_store_unavailable"}});
        }
        result.audit = std::move(receipt);
        return result;
    };
    auto reject = [&](const std::string& reason) {
        ++rejected_;
        return audited(Unknown(job->identity, reason));
    };
    if (timeout.count() <= 0 || timeout.count() > 30000)
        return reject("invalid_timeout");
    if ((request.mode != "review" && request.mode != "filter") ||
        (request.mode == "filter" && !visual::Identity(request.policyId)))
        return reject("invalid_decision_policy");
    if (!job->run || !visual::Identity(job->run->taskId) || !visual::Identity(job->run->runEpoch) ||
        !visual::Identity(job->run->configRevision) || !visual::Identity(request.frameId) ||
        !visual::Identity(request.roiId) || !job->prepare || request.questions.empty() ||
        request.questions.size() > 8)
        return reject("invalid_request");
    std::set<std::string> ids;
    for (const auto& question : request.questions)
        if (!visual::ValidQuestion(question) || !ids.insert(question.itemId).second)
            return reject("invalid_question_reference");
    if (!job->run->Active())
        return reject("stale_task_run");
    if (nativeInference_) {
        if (job->run->atomicCode.empty())
            return reject("model_not_bound");
        for (const auto& question : request.questions)
            if (!question.nativeSpec.is_object() || !visual::Sha256Identity(question.modelIdentity) ||
                question.modelIdentity != request.questions.front().modelIdentity)
                return reject("bound_model_changed");
    }
    if (!nativeInference_ && !options_.release.Valid())
        return reject(options_.unavailableReason);
    if (audit_ && !receipt.Begun())
        return reject("audit_store_unavailable");
    if (Clock::now() >= job->deadline)
        return reject("deadline_exceeded");
    job->identity["deadline_monotonic_ms"] =
        visual::MonotonicMilliseconds() +
        std::chrono::duration_cast<std::chrono::milliseconds>(job->deadline - Clock::now()).count();
    auto future = job->promise.get_future();
    std::string admissionFailure;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_)
            admissionFailure = "service_stopped";
        else if (outstanding_ >= options_.capacity)
            admissionFailure = "queue_full";
        else {
            const auto found = perTask_.find(job->run->taskId);
            if (found != perTask_.end() && found->second >= options_.perTaskLimit)
                admissionFailure = "task_queue_full";
            else {
                queue_.push_back(job);
                ++outstanding_;
                ++perTask_[job->run->taskId];
                ++accepted_;
            }
        }
    }
    if (!admissionFailure.empty())
        return reject(admissionFailure);
    ready_.notify_one();
    if (future.wait_until(job->deadline) != std::future_status::ready)
        Finish(job, Unknown(job->identity, "deadline_exceeded"));
    auto result = future.get();
    if (!job->run->Active())
        result = Unknown(result.request, "stale_task_run");
    if (Clock::now() >= job->deadline)
        result = Unknown(result.request, "deadline_exceeded");
    const auto status = result.response.value("status", std::string());
    if (status == "completed")
        ++completed_;
    else if (status == "partial")
        ++partial_;
    else
        ++unknown_;
    return audited(std::move(result));
}

void VisualDecisionServiceImpl::Finish(const std::shared_ptr<Job>& job, VisualDecisionResult result) {
    if (job->finished.exchange(true))
        return;
    job->promise.set_value(std::move(result));
}

void VisualDecisionServiceImpl::Stop() {
    std::call_once(stopOnce_, [&] {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
            for (const auto& job : queue_)
                Finish(job, Unknown(job->identity, "service_stopped"));
        }
        ready_.notify_all();
        if (worker_.joinable())
            worker_.join();
    });
}

void VisualDecisionServiceImpl::Work() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [&] { return stopped_ || !queue_.empty(); });
            if (queue_.empty() && stopped_)
                return;
            // FIFO within each task, rotating away from the previous task when
            // another is waiting. One busy multi-ROI task cannot take every slot.
            auto next = std::find_if(queue_.begin(), queue_.end(), [&](const auto& candidate) {
                return candidate->run->taskId != lastTask_;
            });
            if (next == queue_.end())
                next = queue_.begin();
            job = *next;
            queue_.erase(next);
            lastTask_ = job->run->taskId;
        }
        if (!job->finished.load()) {
            auto wire   = job->identity;
            auto result = Unknown(wire, "worker_unavailable");
            try {
                if (!job->run->Active())
                    result = Unknown(wire, "stale_task_run");
                else if (Clock::now() >= job->deadline)
                    result = Unknown(wire, "deadline_exceeded");
                else {
                    VisualDecisionImage image;
                    try {
                        image = job->prepare();
                    } catch (...) {
                        throw std::runtime_error("roi_prepare_error");
                    }
                    if (image.jpeg.empty() || image.jpeg.size() > visual::kMaxImage || image.width <= 0 ||
                        image.height <= 0 || image.width > 2048 || image.height > 2048)
                        result = Unknown(wire, "invalid_roi_image");
                    else if (!job->run->Active())
                        result = Unknown(wire, "stale_task_run");
                    else if (Clock::now() >= job->deadline)
                        result = Unknown(wire, "deadline_exceeded");
                    else if (!Available())
                        result = Unknown(wire, "service_stopped");
                    else {
                        wire["image_encoding"] = "jpeg";
                        wire["image_size"]     = image.jpeg.size();
                        wire["image_width"]    = image.width;
                        wire["image_height"]   = image.height;
                        const auto imageSha    = visual::Sha256(image.jpeg.data(), image.jpeg.size());
                        if (nativeInference_)
                            result = {wire,
                                      nativeInference_(wire, job->questions, job->run, image, job->deadline)};
                        else
                            result = {wire,
                                      visual::ValidateResponse(
                                          wire, job->questions, options_.release, imageSha,
                                          transport_(options_.socketPath, wire, image.jpeg, job->deadline))};
                    }
                }
            } catch (const std::exception& e) {
                const std::string message = e.what();
                const bool known = message == "deadline_exceeded" || message == "roi_prepare_error" ||
                                   message == "invalid_worker_response" || message == "invalid_response_size";
                result = Unknown(wire, known              ? message
                                       : nativeInference_ ? "native_inference_failed"
                                                          : "worker_unavailable");
            } catch (...) {
                result = Unknown(wire, nativeInference_ ? "native_inference_failed" : "worker_unavailable");
            }
            if (!job->run->Active())
                result = Unknown(wire, "stale_task_run");
            if (Clock::now() >= job->deadline)
                result = Unknown(wire, "deadline_exceeded");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopped_)
                    result = Unknown(wire, "service_stopped");
            }
            Finish(job, std::move(result));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --outstanding_;
            if (--perTask_.at(job->run->taskId) == 0)
                perTask_.erase(job->run->taskId);
        }
    }
}

nlohmann::json VisualDecisionServiceImpl::Counters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto auditStatus = audit_ ? audit_->Status() : Json{{"available", false}};
    return {{"provider", "laya"},
            {"execution", nativeInference_ ? "in_engine" : "legacy_ipc"},
            {"manifest_sha256", options_.release.manifestSha256},
            {"available", (nativeInference_ || options_.release.Valid()) && !stopped_ &&
                              (!audit_ || auditStatus.value("available", false))},
            {"accepted", accepted_.load()},
            {"rejected", rejected_.load()},
            {"completed", completed_.load()},
            {"partial", partial_.load()},
            {"unknown", unknown_.load()},
            {"outstanding", outstanding_},
            {"queued", queue_.size()},
            {"per_task", perTask_},
            {"audit", auditStatus},
            {"acceptance_status", options_.acceptanceReason},
            {"accepted_profiles", visual::AcceptanceProfiles(options_.acceptance)},
            {"automatic_filtering", !visual::AcceptanceProfiles(options_.acceptance).empty()}};
}
}  // namespace cosmo::service
