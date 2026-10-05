#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>

#include "catch_amalgamated.hpp"
#include "flow/alarm/LayaShadowObserver.h"

namespace {
using cosmo::LayaShadowConfig;
using cosmo::LayaShadowObserver;
using cosmo::LayaShadowPayload;
using cosmo::LayaShadowRun;
using Json = nlohmann::json;

LayaShadowConfig TestConfig() {
    LayaShadowConfig config;
    config.taskId      = "shadow-test-task";
    config.socketPath  = "/tmp/shadow-test.sock";
    config.logPath     = "/tmp/shadow-test.jsonl";
    config.minInterval = std::chrono::milliseconds(0);
    return config;
}
Json Reply(const Json& request) {
    return {{"protocol", "laya-shadow-v1"},
            {"profile", "laya-256p-256s-v1"},
            {"request_id", request.at("request_id")},
            {"run_epoch", request.at("run_epoch")},
            {"decision_status", "unknown"},
            {"reason", "candidate_not_qualified"},
            {"question_id", "helmet-review-en-v1"},
            {"question_version", 1},
            {"ordered_options", {"wearing", "not_wearing", "uncertain"}},
            {"probabilities", {0.2, 0.7, 0.1}}};
}
LayaShadowPayload Image() {
    return {{{"roi_mode", "cropped"}}, {0xff, 0xd8, 0xff, 0xd9}};
}

struct Captures {
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<Json> records;
    void Add(const Json& record) {
        std::lock_guard<std::mutex> lock(mutex);
        records.push_back(record);
        ready.notify_all();
    }
    bool Wait(size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        return ready.wait_for(lock, std::chrono::seconds(2), [&] { return records.size() >= count; });
    }
};

struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{false};
    bool released{false};
    void Enter() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return released; });
    }
    bool Wait() {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; });
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        condition.notify_all();
    }
};

struct UnixServer {
    std::string directory;
    std::string path;
    int listener{-1};
    std::thread thread;
    std::promise<Json> received;
    explicit UnixServer(int delayMs = 0, bool oversized = false) {
        char name[]   = "/tmp/cosmo-laya-test-XXXXXX";
        auto* created = mkdtemp(name);
        if (!created)
            throw std::runtime_error("test directory failed");
        directory = created;
        path      = directory + "/worker.sock";
        listener  = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener, 1) != 0)
            throw std::runtime_error("test socket failed");
        thread = std::thread([&, delayMs, oversized] {
            int fd = accept(listener, nullptr, nullptr);
            try {
                auto read = [fd](void* data, size_t size) {
                    size_t offset = 0;
                    while (offset < size) {
                        ssize_t count = recv(fd, static_cast<char*>(data) + offset, size - offset, 0);
                        if (count <= 0)
                            throw std::runtime_error("test server read failed");
                        offset += static_cast<size_t>(count);
                    }
                };
                uint32_t length = 0;
                read(&length, sizeof(length));
                std::string header(ntohl(length), '\0');
                read(header.data(), header.size());
                auto request = Json::parse(header);
                std::vector<uint8_t> bytes(request.at("image_size").get<size_t>());
                read(bytes.data(), bytes.size());
                request["received_jpeg"] = bytes;
                received.set_value(request);
                if (delayMs)
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                if (delayMs) {
                    close(fd);
                    return;
                }
                std::string response = Reply(request).dump();
                length               = htonl(oversized ? LayaShadowConfig::kMaxJson + 1 : response.size());
                send(fd, &length, sizeof(length), 0);
                if (!oversized)
                    send(fd, response.data(), response.size(), 0);
            } catch (...) {
                try {
                    received.set_exception(std::current_exception());
                } catch (...) {
                }
            }
            close(fd);
        });
    }
    ~UnixServer() {
        if (listener >= 0) {
            shutdown(listener, SHUT_RDWR);
            close(listener);
        }
        if (thread.joinable())
            thread.join();
        unlink(path.c_str());
        rmdir(directory.c_str());
    }
};
}  // namespace

TEST_CASE("Laya shadow is disabled unless exact task and complete private config match", "[laya-shadow]") {
    bool prepared = false;
    LayaShadowObserver disabled({});
    auto run = std::make_shared<LayaShadowRun>("epoch-a");
    REQUIRE(disabled.Submit("shadow-test-task", run, Json::object(), [&] {
        prepared = true;
        return Image();
    }) == "disabled");
    REQUIRE_FALSE(prepared);
    auto config = TestConfig();
    LayaShadowObserver enabled(config, {}, [](const Json&) {});
    REQUIRE(enabled.Submit("another-task", run, Json::object(), [&] {
        prepared = true;
        return Image();
    }) == "disabled");
    REQUIRE_FALSE(prepared);
}

TEST_CASE("Laya shadow admits one inflight and only two pending without running rejected preparation",
          "[laya-shadow]") {
    Gate gate;
    Captures captures;
    auto config = TestConfig();
    LayaShadowObserver observer(
        config,
        [&](const auto&, const Json& request, const auto&) {
            gate.Enter();
            return Reply(request);
        },
        [&](const Json& record) { captures.Add(record); });
    auto run     = std::make_shared<LayaShadowRun>("epoch-a");
    int prepared = 0;
    auto prepare = [&] {
        ++prepared;
        return Image();
    };
    auto first   = observer.Submit(config.taskId, run, Json::object(), prepare);
    bool entered = gate.Wait();
    auto second  = observer.Submit(config.taskId, run, Json::object(), prepare);
    auto third   = observer.Submit(config.taskId, run, Json::object(), prepare);
    auto fourth  = observer.Submit(config.taskId, run, Json::object(), prepare);
    auto fifth   = observer.Submit(config.taskId, run, Json::object(), prepare);
    gate.Release();
    bool allRecorded = captures.Wait(5);
    observer.Stop();
    REQUIRE(entered);
    REQUIRE(first == "accepted");
    REQUIRE(second == "accepted");
    REQUIRE(third == "accepted");
    REQUIRE(fourth == "queue_full");
    REQUIRE(fifth == "queue_full");
    REQUIRE(prepared == 3);
    REQUIRE(allRecorded);
    REQUIRE(observer.Counters()["accepted"] == 3);
    REQUIRE(observer.Counters()["rejected"] == 2);
}

TEST_CASE("Laya shadow rate limiting and bad images cannot invoke inference", "[laya-shadow]") {
    auto config        = TestConfig();
    config.minInterval = std::chrono::seconds(1);
    Captures captures;
    std::atomic<int> calls{0};
    LayaShadowObserver observer(
        config,
        [&](const auto&, const Json& request, const auto&) {
            ++calls;
            return Reply(request);
        },
        [&](const Json& record) { captures.Add(record); });
    auto run = std::make_shared<LayaShadowRun>("epoch-a");
    REQUIRE(observer.Submit(config.taskId, run, Json::object(),
                            [] { return LayaShadowPayload{Json::object(), {}}; }) == "invalid_roi");
    bool prepared = false;
    REQUIRE(observer.Submit(config.taskId, run, Json::object(), [&] {
        prepared = true;
        return Image();
    }) == "rate_limited");
    REQUIRE_FALSE(prepared);
    REQUIRE(captures.Wait(2));
    observer.Stop();
    REQUIRE(calls.load() == 0);
}

TEST_CASE("Laya shadow stale inflight result and queued old epoch never become new task results",
          "[laya-shadow]") {
    Gate gate;
    Captures captures;
    auto config = TestConfig();
    std::atomic<int> calls{0};
    LayaShadowObserver observer(
        config,
        [&](const auto&, const Json& request, const auto&) {
            ++calls;
            gate.Enter();
            return Reply(request);
        },
        [&](const Json& record) { captures.Add(record); });
    auto oldRun  = std::make_shared<LayaShadowRun>("old-epoch");
    auto newRun  = std::make_shared<LayaShadowRun>("new-epoch");
    auto first   = observer.Submit(config.taskId, oldRun, Json::object(), Image);
    bool entered = gate.Wait();
    auto second  = observer.Submit(config.taskId, oldRun, Json::object(), Image);
    oldRun->Invalidate();
    gate.Release();
    bool oldRecorded = captures.Wait(2);
    auto third       = observer.Submit(config.taskId, newRun, Json::object(), Image);
    bool newRecorded = captures.Wait(3);
    observer.Stop();
    REQUIRE(first == "accepted");
    REQUIRE(second == "accepted");
    REQUIRE(third == "accepted");
    REQUIRE(entered);
    REQUIRE(oldRecorded);
    REQUIRE(newRecorded);
    REQUIRE(calls.load() == 2);
    REQUIRE(captures.records[0]["run_epoch"] == "old-epoch");
    REQUIRE(captures.records[0]["result"]["reason"] == "stale_task_run");
    REQUIRE(captures.records[1]["result"]["reason"] == "stale_task_run");
    REQUIRE(captures.records[2]["run_epoch"] == "new-epoch");
    REQUIRE(captures.records[2]["result"]["decision_status"] == "unknown");
}

TEST_CASE("Laya shadow validates identities probabilities and cannot accept active alarm decisions",
          "[laya-shadow]") {
    Json request  = {{"request_id", "r-1"}, {"run_epoch", "e-1"}};
    auto response = Reply(request);
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["decision_status"] == "unknown");
    response["decision_status"] = "confirm_alarm";
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] ==
            "non_shadow_decision_rejected");
    response               = Reply(request);
    response["request_id"] = "r-2";
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] == "worker_identity_mismatch");
    response                  = Reply(request);
    response["probabilities"] = {0.9, 0.9, 0.1};
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] ==
            "invalid_worker_probabilities");
    response                  = Reply(request);
    response["probabilities"] = {nullptr, 0.9, 0.1};
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["decision_status"] == "unavailable");
    response                    = Reply(request);
    response["ordered_options"] = {"a", "b"};
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] ==
            "invalid_worker_probabilities");
}

TEST_CASE("Laya shadow wire protocol sends bounded JPEG and reads length prefixed result", "[laya-shadow]") {
    UnixServer server;
    auto config       = TestConfig();
    config.socketPath = server.path;
    Json request      = {{"request_id", "r-1"}, {"run_epoch", "e-1"}, {"image_size", 4}};
    auto response     = LayaShadowObserver::Exchange(config, request, Image().jpeg);
    auto received     = server.received.get_future().get();
    REQUIRE(received["received_jpeg"] == Json(Image().jpeg));
    REQUIRE(response["request_id"] == "r-1");
    REQUIRE(response["run_epoch"] == "e-1");
}

TEST_CASE("Laya shadow socket deadline and oversized replies are rejected", "[laya-shadow]") {
    SECTION("deadline") {
        UnixServer server(120);
        auto config       = TestConfig();
        config.socketPath = server.path;
        config.timeout    = std::chrono::milliseconds(40);
        Json request      = {{"request_id", "r-1"}, {"run_epoch", "e-1"}, {"image_size", 4}};
        auto start        = std::chrono::steady_clock::now();
        REQUIRE_THROWS_WITH(LayaShadowObserver::Exchange(config, request, Image().jpeg), "worker_timeout");
        REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(250));
    }
    SECTION("oversized") {
        UnixServer server(0, true);
        auto config       = TestConfig();
        config.socketPath = server.path;
        Json request      = {{"request_id", "r-1"}, {"run_epoch", "e-1"}, {"image_size", 4}};
        REQUIRE_THROWS_WITH(LayaShadowObserver::Exchange(config, request, Image().jpeg),
                            "invalid_response_size");
    }
}

TEST_CASE("Laya shadow Stop cancels queued jobs while inflight transport is released", "[laya-shadow]") {
    Gate gate;
    Captures captures;
    auto config = TestConfig();
    std::atomic<int> calls{0};
    LayaShadowObserver observer(
        config,
        [&](const auto&, const Json& request, const auto&) {
            ++calls;
            gate.Enter();
            return Reply(request);
        },
        [&](const Json& record) { captures.Add(record); });
    auto run      = std::make_shared<LayaShadowRun>("epoch-stop");
    auto first    = observer.Submit(config.taskId, run, Json::object(), Image);
    bool entered  = gate.Wait();
    auto second   = observer.Submit(config.taskId, run, Json::object(), Image);
    auto stopping = std::async(std::launch::async, [&] { observer.Stop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    gate.Release();
    stopping.get();
    REQUIRE(first == "accepted");
    REQUIRE(second == "accepted");
    REQUIRE(entered);
    REQUIRE(calls.load() == 1);
    REQUIRE(observer.Counters()["pending"] == 0);
    REQUIRE(captures.records.size() == 2);
    REQUIRE(captures.records[1]["result"]["reason"] == "observer_stopped");
    REQUIRE(observer.Submit(config.taskId, run, Json::object(), Image) == "stopped");
}

TEST_CASE("Laya shadow slow result sink never blocks submission or run invalidation", "[laya-shadow]") {
    Gate sinkGate;
    Captures captures;
    auto config = TestConfig();
    LayaShadowObserver observer(
        config, [&](const auto&, const Json& request, const auto&) { return Reply(request); },
        [&](const Json& record) {
            sinkGate.Enter();
            captures.Add(record);
        });
    auto run            = std::make_shared<LayaShadowRun>("old-epoch-slow-sink");
    auto first          = observer.Submit(config.taskId, run, Json::object(), Image);
    bool sinkEntered    = sinkGate.Wait();
    auto submit         = std::async(std::launch::async,
                                     [&] { return observer.Submit(config.taskId, run, Json::object(), Image); });
    bool submitReturned = submit.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready;
    auto invalidate     = std::async(std::launch::async, [&] { run->Invalidate(); });
    bool invalidateReturned =
        invalidate.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready;
    sinkGate.Release();
    auto second = submit.get();
    invalidate.get();
    bool recorded = captures.Wait(2);
    observer.Stop();
    REQUIRE(first == "accepted");
    REQUIRE(sinkEntered);
    REQUIRE(submitReturned);
    REQUIRE(invalidateReturned);
    REQUIRE(second == "accepted");
    REQUIRE(recorded);
    REQUIRE(captures.records[0]["run_epoch"] == "old-epoch-slow-sink");
    REQUIRE(captures.records[0]["run_active_at_result_snapshot"] == true);
    REQUIRE(captures.records[1]["run_epoch"] == "old-epoch-slow-sink");
    REQUIRE(captures.records[1]["result"]["reason"] == "stale_task_run");
}

TEST_CASE("Laya shadow rejects wrong question version option order and top1", "[laya-shadow]") {
    Json request            = {{"request_id", "r-1"}, {"run_epoch", "e-1"}};
    auto response           = Reply(request);
    response["question_id"] = "other-question";
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] == "worker_identity_mismatch");
    for (const auto& version : Json::array({"1", 1.0, true, 2})) {
        response                     = Reply(request);
        response["question_version"] = version;
        REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] ==
                "worker_identity_mismatch");
    }
    response                    = Reply(request);
    response["ordered_options"] = {"not_wearing", "wearing", "uncertain"};
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] ==
            "invalid_worker_probabilities");
    response         = Reply(request);
    response["top1"] = "not_wearing";
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["decision_status"] == "unknown");
    response["top1"] = "wearing";
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] == "invalid_worker_top1");
    response["top1"] = "other";
    REQUIRE(LayaShadowObserver::ValidateResponse(request, response)["reason"] == "invalid_worker_top1");
}
