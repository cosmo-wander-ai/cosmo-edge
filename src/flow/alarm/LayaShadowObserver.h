#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace cosmo {

// Private experimental observer. It never owns TaskAlarm or returns a filtering decision.
struct LayaShadowRun {
    explicit LayaShadowRun(std::string value) : epoch(std::move(value)) {}
    void Invalidate();
    const std::string epoch;
    std::mutex mutex;
    bool active{true};
};

struct LayaShadowConfig {
    std::string taskId;
    std::string socketPath;
    std::string logPath;
    size_t pendingLimit{2};
    std::chrono::milliseconds minInterval{1000};
    std::chrono::milliseconds timeout{1500};
    static constexpr size_t kMaxJson  = 16 * 1024;
    static constexpr size_t kMaxImage = 2 * 1024 * 1024;
    static LayaShadowConfig FromEnvironment();
    bool Enabled() const;
};

struct LayaShadowPayload {
    nlohmann::json metadata;
    std::vector<uint8_t> jpeg;
};

class LayaShadowObserver {
public:
    using Json      = nlohmann::json;
    using Transport = std::function<Json(const LayaShadowConfig&, const Json&, const std::vector<uint8_t>&)>;
    using Sink      = std::function<void(const Json&)>;
    using Prepare   = std::function<LayaShadowPayload()>;

    explicit LayaShadowObserver(LayaShadowConfig config, Transport transport = {}, Sink sink = {});
    ~LayaShadowObserver();
    LayaShadowObserver(const LayaShadowObserver&)            = delete;
    LayaShadowObserver& operator=(const LayaShadowObserver&) = delete;
    static LayaShadowObserver& Instance();
    bool EnabledForTask(const std::string& taskId) const;

    // Reserves the rate/queue slot before calling prepare, synchronously. No socket or disk I/O
    // runs on the submitting thread, and only independently owned JPEG bytes enter the queue.
    std::string Submit(const std::string& taskId, const std::shared_ptr<LayaShadowRun>& run, Json identity,
                       const Prepare& prepare);
    Json Counters() const;
    void Stop();
    static Json Exchange(const LayaShadowConfig& config, const Json& metadata,
                         const std::vector<uint8_t>& jpeg);
    static Json ValidateResponse(const Json& request, const Json& response);

private:
    struct Job {
        Json metadata;
        std::vector<uint8_t> jpeg;
        std::shared_ptr<LayaShadowRun> run;
        std::chrono::steady_clock::time_point submitted;
    };
    void Run();
    void Record(const Json& record);
    void RejectLocked(Json identity, const std::string& reason);
    LayaShadowConfig config_;
    Transport transport_;
    Sink sink_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Job> pending_;
    std::deque<Json> diagnostics_;
    size_t preparing_{0};
    bool stopped_{false};
    std::chrono::steady_clock::time_point nextAllowed_{};
    std::thread worker_;
    uint64_t sequence_{0};
    uint64_t accepted_{0};
    uint64_t completed_{0};
    uint64_t rejected_{0};
    uint64_t diagnosticDrops_{0};
    std::atomic<uint64_t> logFailures_{0};
};

}  // namespace cosmo
