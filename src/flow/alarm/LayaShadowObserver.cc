#include "flow/alarm/LayaShadowObserver.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace cosmo {
namespace {
    using Json  = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    struct FileDescriptor {
        int value{-1};
        ~FileDescriptor() {
            if (value >= 0)
                close(value);
        }
    };

    void Await(int fd, short events, Clock::time_point deadline) {
        for (;;) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            if (remaining.count() <= 0)
                throw std::runtime_error("worker_timeout");
            pollfd pfd{fd, events, 0};
            int rc = poll(&pfd, 1, static_cast<int>(remaining.count()));
            if (rc < 0 && errno == EINTR)
                continue;
            if (rc <= 0)
                throw std::runtime_error(rc == 0 ? "worker_timeout" : "worker_io_error");
            if (pfd.revents & events)
                return;
            throw std::runtime_error("worker_disconnected");
        }
    }

    void Transfer(int fd, void* buffer, size_t length, bool writing, Clock::time_point deadline) {
        auto* bytes = static_cast<uint8_t*>(buffer);
        while (length > 0) {
            Await(fd, writing ? POLLOUT : POLLIN, deadline);
            ssize_t count;
            if (writing) {
#ifdef MSG_NOSIGNAL
                count = send(fd, bytes, length, MSG_NOSIGNAL);
#else
                count = send(fd, bytes, length, 0);
#endif
            } else {
                count = recv(fd, bytes, length, 0);
            }
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            if (count <= 0)
                throw std::runtime_error("worker_disconnected");
            bytes += count;
            length -= static_cast<size_t>(count);
        }
    }

    Json Unavailable(const std::string& reason) {
        return {{"decision_status", "unavailable"}, {"reason", reason}};
    }

    bool Active(const std::shared_ptr<LayaShadowRun>& run) {
        if (!run)
            return false;
        std::lock_guard<std::mutex> lock(run->mutex);
        return run->active;
    }
}  // namespace

void LayaShadowRun::Invalidate() {
    std::lock_guard<std::mutex> lock(mutex);
    active = false;
}

LayaShadowConfig LayaShadowConfig::FromEnvironment() {
    LayaShadowConfig config;
    auto read = [](const char* key) {
        const char* value = std::getenv(key);
        return value ? std::string(value) : std::string();
    };
    config.taskId     = read("COSMO_LAYA_SHADOW_TASK_ID");
    config.socketPath = read("COSMO_LAYA_SHADOW_SOCKET");
    config.logPath    = read("COSMO_LAYA_SHADOW_LOG");
    return config;
}

bool LayaShadowConfig::Enabled() const {
    return !taskId.empty() && taskId.size() <= 256 && !socketPath.empty() && socketPath.front() == '/' &&
           socketPath.size() < sizeof(sockaddr_un::sun_path) && !logPath.empty() && logPath.front() == '/' &&
           pendingLimit > 0 && pendingLimit <= 2 && minInterval.count() >= 0 && timeout.count() > 0 &&
           timeout.count() <= 1500;
}

LayaShadowObserver::LayaShadowObserver(LayaShadowConfig config, Transport transport, Sink sink)
    : config_(std::move(config)), transport_(std::move(transport)), sink_(std::move(sink)) {
    if (!transport_)
        transport_ = Exchange;
    if (config_.Enabled())
        worker_ = std::thread(&LayaShadowObserver::Run, this);
}
LayaShadowObserver::~LayaShadowObserver() {
    Stop();
}
LayaShadowObserver& LayaShadowObserver::Instance() {
    static LayaShadowObserver observer(LayaShadowConfig::FromEnvironment());
    return observer;
}
bool LayaShadowObserver::EnabledForTask(const std::string& taskId) const {
    return config_.Enabled() && config_.taskId == taskId;
}

void LayaShadowObserver::RejectLocked(Json identity, const std::string& reason) {
    ++rejected_;
    identity["result"]      = Unavailable(reason);
    identity["event"]       = "shadow_result";
    identity["shadow_only"] = true;
    if (diagnostics_.size() >= 64) {
        ++diagnosticDrops_;
        return;
    }
    diagnostics_.push_back(std::move(identity));
    ready_.notify_one();
}

std::string LayaShadowObserver::Submit(const std::string& taskId, const std::shared_ptr<LayaShadowRun>& run,
                                       Json identity, const Prepare& prepare) {
    if (!EnabledForTask(taskId))
        return "disabled";
    if (!Active(run))
        return "stale_task_run";
    auto submitted = Clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        identity["protocol"]   = "laya-shadow-v1";
        identity["task_id"]    = taskId;
        identity["run_epoch"]  = run->epoch;
        identity["request_id"] = run->epoch + ":" + std::to_string(++sequence_);
        if (stopped_)
            return "stopped";
        if (pending_.size() + preparing_ >= config_.pendingLimit) {
            RejectLocked(std::move(identity), "queue_full");
            return "queue_full";
        }
        if (submitted < nextAllowed_) {
            RejectLocked(std::move(identity), "rate_limited");
            return "rate_limited";
        }
        nextAllowed_ = submitted + config_.minInterval;
        ++preparing_;
    }
    const auto requestId = identity.at("request_id");
    LayaShadowPayload payload;
    std::string failure;
    try {
        payload = prepare();
        if (payload.jpeg.empty())
            failure = "invalid_roi";
        else if (payload.jpeg.size() > LayaShadowConfig::kMaxImage)
            failure = "image_too_large";
        if (!payload.metadata.is_object())
            failure = "invalid_metadata";
        for (auto it = payload.metadata.begin(); it != payload.metadata.end(); ++it)
            identity[it.key()] = it.value();
        // Preparation cannot replace source identity or the wire contract.
        identity["protocol"]         = "laya-shadow-v1";
        identity["task_id"]          = taskId;
        identity["run_epoch"]        = run->epoch;
        identity["request_id"]       = requestId;
        identity["image_size"]       = payload.jpeg.size();
        identity["image_encoding"]   = "jpeg";
        identity["question_id"]      = "helmet-review-en-v1";
        identity["question_version"] = 1;
        identity["profile"]          = "laya-256p-256s-v1";
        identity["prepare_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - submitted).count();
        if (identity.dump().size() > LayaShadowConfig::kMaxJson)
            failure = "metadata_too_large";
    } catch (...) {
        failure = "roi_prepare_error";
    }
    bool active = Active(run);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        --preparing_;
        if (stopped_)
            return "stopped";
        if (!active)
            failure = "stale_task_run";
        if (!failure.empty()) {
            RejectLocked(std::move(identity), failure);
            return failure;
        }
        pending_.push_back({std::move(identity), std::move(payload.jpeg), run, submitted});
        ++accepted_;
        ready_.notify_one();
    }
    return "accepted";
}

LayaShadowObserver::Json LayaShadowObserver::Counters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {{"accepted", accepted_},
            {"completed", completed_},
            {"rejected", rejected_},
            {"pending", pending_.size()},
            {"preparing", preparing_},
            {"diagnostic_drops", diagnosticDrops_},
            {"log_failures", logFailures_.load()}};
}

void LayaShadowObserver::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        for (auto& job : pending_)
            RejectLocked(std::move(job.metadata), "observer_stopped");
        pending_.clear();
    }
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void LayaShadowObserver::Run() {
    for (;;) {
        Job job;
        Json diagnostic;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [&] { return stopped_ || !pending_.empty() || !diagnostics_.empty(); });
            if (!diagnostics_.empty()) {
                diagnostic = std::move(diagnostics_.front());
                diagnostics_.pop_front();
            } else if (stopped_)
                return;
            else {
                job = std::move(pending_.front());
                pending_.pop_front();
            }
        }
        if (!diagnostic.is_null()) {
            Record(diagnostic);
            continue;
        }
        Json response;
        try {
            if (!Active(job.run))
                response = Unavailable("stale_task_run");
            else {
                const auto queued             = Clock::now() - job.submitted;
                job.metadata["queue_wait_ms"] = std::chrono::duration<double, std::milli>(queued).count() -
                                                job.metadata.value("prepare_ms", 0.0);
                auto transportConfig = config_;
                transportConfig.timeout -= std::chrono::duration_cast<std::chrono::milliseconds>(queued);
                if (transportConfig.timeout.count() <= 0)
                    response = Unavailable("queue_deadline_exceeded");
                else
                    response =
                        ValidateResponse(job.metadata, transport_(transportConfig, job.metadata, job.jpeg));
            }
        } catch (const std::exception& e) {
            // Only local transport errors are recorded; arbitrary worker exception text is not a log path.
            const std::string message = e.what();
            response = Unavailable(message == "worker_timeout" ? message : "worker_unavailable");
        } catch (...) {
            response = Unavailable("worker_unavailable");
        }
        job.jpeg.clear();
        job.metadata["elapsed_ms"] =
            std::chrono::duration<double, std::milli>(Clock::now() - job.submitted).count();
        job.metadata["event"]       = "shadow_result";
        job.metadata["shadow_only"] = true;
        // Capture lifecycle state without holding its lock across disk I/O or an external sink.
        // This append-only diagnostic always retains the original epoch and never calls TaskAlarm.
        // Invalidation after this snapshot may race with the append, not with a new task's state.
        const bool activeAtSnapshot = Active(job.run);
        if (!activeAtSnapshot)
            response = Unavailable("stale_task_run");
        job.metadata["run_active_at_result_snapshot"] = activeAtSnapshot;
        job.metadata["result"]                        = std::move(response);
        Record(job.metadata);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++completed_;
        }
    }
}

void LayaShadowObserver::Record(const Json& record) {
    try {
        if (sink_) {
            sink_(record);
            return;
        }
        auto encoded = record.dump() + "\n";
        FileDescriptor fd{
            open(config_.logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600)};
        struct stat st {};
        if (fd.value < 0 || fstat(fd.value, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
            (st.st_mode & 077) != 0 || st.st_size + static_cast<off_t>(encoded.size()) > 16 * 1024 * 1024) {
            ++logFailures_;
            return;
        }
        size_t written = 0;
        while (written < encoded.size()) {
            auto count = write(fd.value, encoded.data() + written, encoded.size() - written);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                ++logFailures_;
                return;
            }
            written += static_cast<size_t>(count);
        }
    } catch (...) {
        ++logFailures_;
    }
}

LayaShadowObserver::Json LayaShadowObserver::ValidateResponse(const Json& request, const Json& response) {
    try {
        if (!response.is_object() || response.at("protocol") != "laya-shadow-v1" ||
            response.at("request_id") != request.at("request_id") ||
            response.at("run_epoch") != request.at("run_epoch") ||
            response.at("profile") != "laya-256p-256s-v1" ||
            response.at("question_id") != "helmet-review-en-v1" ||
            !response.at("question_version").is_number_integer() || response.at("question_version") != 1)
            return Unavailable("worker_identity_mismatch");
        auto status = response.at("decision_status").get<std::string>();
        if (status != "unknown" && status != "unavailable")
            return Unavailable("non_shadow_decision_rejected");
        if (!response.at("reason").is_string())
            return Unavailable("invalid_worker_response");
        if (response.contains("probabilities")) {
            const auto& probabilities = response.at("probabilities");
            const auto& options       = response.at("ordered_options");
            if (!probabilities.is_array() || probabilities.size() != 3 ||
                options != Json::array({"wearing", "not_wearing", "uncertain"}))
                return Unavailable("invalid_worker_probabilities");
            double sum  = 0;
            size_t best = 0;
            for (size_t i = 0; i < probabilities.size(); ++i) {
                double value = probabilities.at(i).get<double>();
                if (!std::isfinite(value) || value < 0 || value > 1 || !options.at(i).is_string())
                    return Unavailable("invalid_worker_probabilities");
                sum += value;
                if (value > probabilities.at(best).get<double>())
                    best = i;
            }
            if (std::abs(sum - 1.0) > 0.0001)
                return Unavailable("invalid_worker_probabilities");
            if (response.contains("top1") && response.at("top1") != options.at(best))
                return Unavailable("invalid_worker_top1");
        } else if (response.contains("ordered_options") || response.contains("top1")) {
            return Unavailable("invalid_worker_probabilities");
        }
        return response;
    } catch (...) {
        return Unavailable("invalid_worker_response");
    }
}

LayaShadowObserver::Json LayaShadowObserver::Exchange(const LayaShadowConfig& config, const Json& metadata,
                                                      const std::vector<uint8_t>& jpeg) {
    std::string encoded = metadata.dump();
    if (encoded.empty() || encoded.size() > LayaShadowConfig::kMaxJson || jpeg.empty() ||
        jpeg.size() > LayaShadowConfig::kMaxImage ||
        config.socketPath.size() >= sizeof(sockaddr_un::sun_path))
        throw std::runtime_error("invalid_request_size");
    auto deadline = Clock::now() + config.timeout;
    FileDescriptor fd{socket(AF_UNIX, SOCK_STREAM, 0)};
    if (fd.value < 0)
        throw std::runtime_error("worker_socket_error");
    if (fcntl(fd.value, F_SETFL, O_NONBLOCK) != 0 || fcntl(fd.value, F_SETFD, FD_CLOEXEC) != 0)
        throw std::runtime_error("worker_socket_error");
#ifdef SO_NOSIGPIPE
    int noSigPipe = 1;
    setsockopt(fd.value, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, config.socketPath.c_str(), config.socketPath.size() + 1);
    int connected = connect(fd.value, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (connected < 0) {
        if (errno != EINPROGRESS && errno != EAGAIN)
            throw std::runtime_error("worker_connect_error");
        Await(fd.value, POLLOUT, deadline);
        int error             = 0;
        socklen_t errorLength = sizeof(error);
        if (getsockopt(fd.value, SOL_SOCKET, SO_ERROR, &error, &errorLength) != 0 || error != 0)
            throw std::runtime_error("worker_connect_error");
    }
    uint32_t length = htonl(static_cast<uint32_t>(encoded.size()));
    Transfer(fd.value, &length, sizeof(length), true, deadline);
    Transfer(fd.value, encoded.data(), encoded.size(), true, deadline);
    Transfer(fd.value, const_cast<uint8_t*>(jpeg.data()), jpeg.size(), true, deadline);
    Transfer(fd.value, &length, sizeof(length), false, deadline);
    auto responseLength = ntohl(length);
    if (responseLength == 0 || responseLength > LayaShadowConfig::kMaxJson)
        throw std::runtime_error("invalid_response_size");
    std::string response(responseLength, '\0');
    Transfer(fd.value, response.data(), response.size(), false, deadline);
    return Json::parse(response);
}
}  // namespace cosmo
