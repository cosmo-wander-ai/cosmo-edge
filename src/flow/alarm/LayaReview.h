#pragma once
#include <future>

#include "flow/alarm/LayaShadowObserver.h"
#include "service/event/LayaReviewStore.h"
namespace cosmo {
class LayaReview {
public:
    using Json      = nlohmann::json;
    using Prepare   = LayaShadowObserver::Prepare;
    using Transport = LayaShadowObserver::Transport;
    explicit LayaReview(LayaReviewStore& store, Transport transport = {},
                        std::string socket = "/run/cosmo-laya/worker.sock");
    ~LayaReview();
    static LayaReview& Instance();
    // true means keep the alarm. Only a durable, qualified reject may return false.
    bool Submit(Json identity, std::shared_ptr<LayaShadowRun> run, Prepare prepare, bool review,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));
    static Json Classify(const Json& request, const Json& response, bool qualified);

private:
    struct Job {
        Json identity;
        std::shared_ptr<LayaShadowRun> run;
        Prepare prepare;
        bool review;
        std::chrono::steady_clock::time_point deadline;
        std::promise<Json> promise;
    };
    void Work();
    LayaReviewStore& store_;
    Transport transport_;
    std::string socket_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::shared_ptr<Job>> queue_;
    size_t active_{0};
    bool stop_{false};
    std::thread worker_;
};
}  // namespace cosmo
