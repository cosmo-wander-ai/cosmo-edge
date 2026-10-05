#include "flow/alarm/LayaReview.h"

#include <cmath>
#include <cstdlib>
namespace cosmo {
namespace {
    using Json  = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    Json Unknown(const std::string& reason) {
        return {{"decision", "unknown"}, {"reason", reason}, {"policy_version", "helmet-conservative-v1"}};
    }
    bool Active(const std::shared_ptr<LayaShadowRun>& run) {
        if (!run)
            return false;
        std::lock_guard<std::mutex> l(run->mutex);
        return run->active;
    }
}  // namespace
LayaReview::LayaReview(LayaReviewStore& store, Transport transport, std::string socket)
    : store_(store),
      transport_(transport ? std::move(transport) : LayaShadowObserver::Exchange),
      socket_(std::move(socket)),
      worker_(&LayaReview::Work, this) {}
LayaReview::~LayaReview() {
    {
        std::lock_guard<std::mutex> l(mutex_);
        stop_ = true;
    }
    ready_.notify_all();
    worker_.join();
}
LayaReview& LayaReview::Instance() {
    static LayaReview service(LayaReviewStore::Instance());
    return service;
}
LayaReview::Json LayaReview::Classify(const Json& request, const Json& response, bool qualified) {
    auto r = LayaShadowObserver::ValidateResponse(request, response);
    if (r.value("decision_status", std::string()) == "unavailable")
        return Unknown(r.value("reason", std::string("worker_unavailable")));
    if (!r.contains("probabilities"))
        return Unknown("missing_probabilities");
    const Json hashes = {{"tower", "41c82b46528e171686faac6b76c408b31abba7d7986ac8c799d600e9ee190aff"},
                         {"adapter", "d948ff1fecaf790cd17b327ae99bf2afa42dc53d9ed958a317ff6e33fe1c39e9"},
                         {"decision", "4bf88150317d05e23e7c29e831287684f702a829d4d0975a5ef1811a6417c19e"}};
    if (r.value("model_hashes", Json::object()) != hashes)
        return Unknown("model_identity_mismatch");
    auto out               = Unknown("business_qualification_pending");
    out["probabilities"]   = r["probabilities"];
    out["ordered_options"] = r["ordered_options"];
    out["model_hashes"]    = hashes;
    out["numeric_status"]  = "7_of_8_exception_retained";
    if (r.contains("timing_ms"))
        out["timing_ms"] = r["timing_ms"];
    if (!qualified)
        return out;
    double p0 = r["probabilities"][0], p1 = r["probabilities"][1], p2 = r["probabilities"][2];
    if (p0 >= 0.98 && p0 - std::max(p1, p2) >= 0.5) {
        out["decision"] = "reject";
        out["reason"]   = "helmet_present";
    } else if (p1 >= 0.98 && p1 - std::max(p0, p2) >= 0.5) {
        out["decision"] = "confirm";
        out["reason"]   = "helmet_absent";
    } else
        out["reason"] = "uncertain_or_low_confidence";
    return out;
}
bool LayaReview::Submit(Json identity, std::shared_ptr<LayaShadowRun> run, Prepare prepare, bool review,
                        std::chrono::milliseconds timeout) {
    auto job             = std::make_shared<Job>();
    job->deadline        = Clock::now() + timeout;
    job->identity        = std::move(identity);
    job->run             = std::move(run);
    job->prepare         = std::move(prepare);
    job->review          = review;
    const std::string id = job->identity.at("event_id");
    if (!store_.Begin(job->identity))
        return true;
    auto future = job->promise.get_future();
    {
        std::lock_guard<std::mutex> l(mutex_);
        if (stop_ || active_ + queue_.size() >= 3) {
            store_.Finish(id, Unknown("queue_full"));
            return true;
        }
        queue_.push_back(job);
    }
    ready_.notify_one();
    if (!review)
        return true;
    auto result = Unknown("deadline_exceeded");
    if (future.wait_until(job->deadline) == std::future_status::ready)
        result = future.get();
    if (!Active(job->run))
        result = Unknown("stale_task_run");
    if (Clock::now() >= job->deadline)
        result = Unknown("deadline_exceeded");
    // Result is immutable after first completion; a late worker never rewrites this record.
    if (!store_.Finish(id, result))
        return true;
    return result.value("decision", std::string("unknown")) != "reject";
}
void LayaReview::Work() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> l(mutex_);
            ready_.wait(l, [&] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty())
                return;
            job = queue_.front();
            queue_.pop_front();
            ++active_;
        }
        Json result = Unknown("worker_unavailable");
        try {
            if (!Active(job->run))
                result = Unknown("stale_task_run");
            else if (Clock::now() >= job->deadline)
                result = Unknown("deadline_exceeded");
            else {
                auto payload = job->prepare();
                auto request = job->identity;
                request.update(payload.metadata);
                request["protocol"]         = "laya-shadow-v1";
                request["profile"]          = "laya-256p-256s-v1";
                request["question_id"]      = "helmet-review-en-v1";
                request["question_version"] = 1;
                request["image_encoding"]   = "jpeg";
                request["image_size"]       = payload.jpeg.size();
                request["request_id"]       = request["event_id"];
                LayaShadowConfig config;
                config.socketPath = socket_;
                config.timeout =
                    std::chrono::duration_cast<std::chrono::milliseconds>(job->deadline - Clock::now());
                if (payload.jpeg.empty())
                    result = Unknown("unsupported_or_empty_roi");
                else if (config.timeout.count() <= 0)
                    result = Unknown("deadline_exceeded");
                // Business qualification must be supported by a later labelled acceptance package.
                // This candidate ships with the numerical exception and cannot auto-filter by default.
                else
                    result = Classify(request, transport_(config, request, payload.jpeg), false);
            }
        } catch (const std::exception& e) {
            result = Unknown(std::string(e.what()) == "worker_timeout" ? "deadline_exceeded"
                                                                       : "worker_unavailable");
        } catch (...) {
            result = Unknown("worker_unavailable");
        }
        if (!Active(job->run))
            result = Unknown("stale_task_run");
        if (Clock::now() >= job->deadline)
            result = Unknown("deadline_exceeded");
        if (!job->review)
            store_.Finish(job->identity.at("event_id"), result);
        job->promise.set_value(std::move(result));
        {
            std::lock_guard<std::mutex> l(mutex_);
            --active_;
        }
    }
}
}  // namespace cosmo
