#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <thread>

#include "catch_amalgamated.hpp"
#include "service/ai/impl/VisualDecisionServiceImpl.h"
#include "util/UuidUtil.h"

using namespace std::chrono_literals;
namespace {
using namespace cosmo::service;
using Json  = nlohmann::json;
using Clock = visual::Clock;

VisualDecisionOptions Options(size_t capacity = 3, size_t perTask = 2) {
    VisualDecisionOptions options;
    options.release      = {std::string(64, 'a'),
                            {{"tower", std::string(64, 'b')},
                             {"adapter", std::string(64, 'c')},
                             {"decision", std::string(64, 'd')}}};
    options.capacity     = capacity;
    options.perTaskLimit = perTask;
    return options;
}

VisualQuestionRef Question(const std::string& item = "helmet") {
    return {item,
            "is-helmet-present",
            1,
            std::string(64, 'e'),
            2,
            {"false", "true"},
            {{"bucket", "image|noul:2"}, {"source", "temperature_image"}, {"value", 1.0}}};
}

auto Run(const std::string& task = "task-1", const std::string& epoch = "epoch-1") {
    return std::make_shared<VisualDecisionRun>(task, epoch, "config-3");
}

VisualDecisionRequest Request() {
    return {"frame-5", "roi-east", {Question()}};
}

VisualDecisionImage Image() {
    return {{1, 2, 3}, 640, 480};
}

Json Reply(const Json& request, const std::vector<uint8_t>& jpeg) {
    auto result = visual::Failure(request, "unused");
    result.erase("reason");
    result["status"]       = "completed";
    result["model_hashes"] = Options().release.modelHashes;
    result["image_sha256"] = visual::Sha256(jpeg.data(), jpeg.size());
    for (auto& row : result["items"]) {
        row.erase("reason");
        row["status"]             = "completed";
        row["ordered_options"]    = {"false", "true"};
        row["qtype"]              = 2;
        row["temperature"]        = Question().temperature;
        row["probabilities"]      = {0.1, 0.9};
        row["raw_option_logits"]  = {std::log(0.1), std::log(0.9)};
        row["raw_action_logits"]  = {0.0, 1.0};
        row["top1"]               = "true";
        row["top2_margin"]        = 0.8;
        row["decision_timing_ms"] = {
            {"h2d_ms", 1.0}, {"launch_sync_ms", 38.0}, {"d2h_ms", 1.0}, {"total_ms", 40.0}};
    }
    return result;
}

auto Transport = [](const auto&, const Json& request, const auto& jpeg, auto) {
    return Reply(request, jpeg);
};

// Controlled gates, not scheduler-dependent sleeps, hold one transport in flight.
struct Gate {
    std::promise<void> entered;
    std::promise<void> released;
    std::shared_future<void> release{released.get_future().share()};
    ~Gate() {
        Open();
    }
    void Open() {
        try {
            released.set_value();
        } catch (const std::future_error&) {
        }
    }
};

bool Queued(VisualDecisionServiceImpl& service, size_t count) {
    const auto deadline = Clock::now() + 2s;
    while (Clock::now() < deadline) {
        if (service.Counters()["queued"] == count)
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

class SocketPeer {
public:
    const std::string path =
        (std::filesystem::temp_directory_path() / ("visual-" + cosmo::util::GenerateUUID() + ".sock"))
            .string();
    using Handler = std::function<void(int, const Json&, const std::vector<uint8_t>&)>;
    explicit SocketPeer(Handler handler) {
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0)
            throw std::runtime_error("socket creation failed");
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path))
            throw std::runtime_error("socket path too long");
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) || listen(fd_, 1))
            throw std::runtime_error("socket bind failed");
        thread_ = std::thread([this, handler] {
            int client = accept(fd_, nullptr, nullptr);
            if (client < 0)
                return;
            try {
                uint32_t size = 0;
                Read(client, &size, sizeof(size));
                std::string encoded(ntohl(size), '\0');
                Read(client, encoded.data(), encoded.size());
                auto request = visual::Parse(encoded);
                std::vector<uint8_t> jpeg(request.at("image_size").get<size_t>());
                Read(client, jpeg.data(), jpeg.size());
                handler(client, request, jpeg);
            } catch (...) {
                error_ = std::current_exception();
            }
            close(client);
        });
    }
    ~SocketPeer() {
        shutdown(fd_, SHUT_RDWR);
        if (thread_.joinable())
            thread_.join();
        close(fd_);
        std::filesystem::remove(path);
    }
    void Check() {
        thread_.join();
        if (error_)
            std::rethrow_exception(error_);
    }
    static void Write(int fd, const std::string& payload) {
        uint32_t size = htonl(static_cast<uint32_t>(payload.size()));
        if (send(fd, &size, sizeof(size), MSG_NOSIGNAL) != sizeof(size))
            throw std::runtime_error("reply header failed");
        size_t done = 0;
        while (done < payload.size()) {
            auto sent =
                send(fd, payload.data() + done, std::min<size_t>(11, payload.size() - done), MSG_NOSIGNAL);
            if (sent <= 0)
                throw std::runtime_error("reply body failed");
            done += static_cast<size_t>(sent);
        }
    }

private:
    static void Read(int fd, void* data, size_t size) {
        auto* p = static_cast<char*>(data);
        while (size) {
            const auto received = recv(fd, p, size, 0);
            if (received <= 0)
                throw std::runtime_error("request truncated");
            p += received;
            size -= static_cast<size_t>(received);
        }
    }
    int fd_{-1};
    std::thread thread_;
    std::exception_ptr error_;
};
}  // namespace

TEST_CASE("Visual decision preserves independent items and rejects forged result semantics",
          "[visual-decision]") {
    const int mutation = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    auto request       = Request();
    request.questions.push_back(Question("second-question"));
    VisualDecisionServiceImpl service(Options(), [&](const auto&, const Json& wire, const auto& jpeg, auto) {
        auto response = Reply(wire, jpeg);
        switch (mutation) {
            case 1:
                response["roi_id"] = "another-roi";
                break;
            case 2:
                response["items"][1]["question_version"] = 2;
                break;
            case 3:
                response["items"][0]["ordered_options"] = {"true", "false"};
                break;
            case 4:
                response["items"][0]["probabilities"] = {0.2, 0.9};
                break;
            case 5:
                response["items"][0]["raw_option_logits"][0] = 5.0;
                break;
            case 6:
                response["items"][0]["business_qualified"] = true;
                break;
            case 7:
                response["model_hashes"]["tower"] = std::string(64, 'f');
                break;
            case 8:
                response["image_sha256"] = std::string(64, 'f');
                break;
            case 9:
                response["items"][0]["top2_margin"] = 0.3;
                break;
            case 10:
                response["items"][0]["probabilities"][0] = std::numeric_limits<double>::quiet_NaN();
                break;
            case 11:
                response["items"][1] = response["items"][0];
                break;
            case 12:
                response["items"][0]["temperature"]["value"] = 2.0;
                break;
            case 13:
                response["items"][0]["decision_timing_ms"] = 40.0;
                break;
        }
        return response;
    });
    const auto result = service.Decide(request, Run(), Image, 1500ms);
    REQUIRE(result.request["roi_id"] == "roi-east");
    REQUIRE(result.response["items"].size() == 2);
    REQUIRE(result.response["items"][1]["item_id"] == "second-question");
    REQUIRE(result.response["items"][0]["business_qualified"] == false);
    REQUIRE(result.AllCompleted() == (mutation == 0));
    REQUIRE(service.Counters()["automatic_filtering"] == false);
}

TEST_CASE("Visual worker unknown items cannot borrow another item's success", "[visual-decision]") {
    auto request = Request();
    request.questions.push_back(Question("missing"));
    VisualDecisionServiceImpl service(Options(), [](const auto&, const Json& wire, const auto& jpeg, auto) {
        auto response        = Reply(wire, jpeg);
        response["status"]   = "partial";
        response["items"][1] = visual::Failure(wire, "question_missing_or_mismatched")["items"][1];
        return response;
    });
    auto result = service.Decide(request, Run(), Image, 1500ms);
    REQUIRE_FALSE(result.AllCompleted());
    REQUIRE(result.response["status"] == "partial");
    REQUIRE(result.response["items"][0]["status"] == "completed");
    REQUIRE(result.response["items"][1]["status"] == "unknown");
    REQUIRE_FALSE(result.response["items"][1].contains("probabilities"));
}

TEST_CASE("Visual admission fails before image work for invalid references and absent release",
          "[visual-decision]") {
    const int invalid = GENERATE(0, 1, 2, 3, 4, 5);
    auto options      = Options();
    auto request      = Request();
    if (invalid == 0)
        options.release = {};
    if (invalid == 1)
        request.questions.push_back(request.questions[0]);
    if (invalid == 2)
        request.questions[0].compiledSha256 = "../question.json";
    if (invalid == 3)
        request.questions[0].orderedOptions = {"true", "false"};
    if (invalid == 4)
        request.roiId.clear();
    if (invalid == 5)
        request.questions.resize(9, Question());
    std::atomic<int> prepares{0}, calls{0};
    VisualDecisionServiceImpl service(options, [&](const auto&, const auto& wire, const auto& jpeg, auto) {
        ++calls;
        return Reply(wire, jpeg);
    });
    const auto result = service.Decide(
        request, Run(),
        [&] {
            ++prepares;
            return Image();
        },
        1500ms);
    REQUIRE_FALSE(result.AllCompleted());
    REQUIRE(prepares == 0);
    REQUIRE(calls == 0);
    REQUIRE(service.Counters()["rejected"] == 1);
}

TEST_CASE("Visual queue reserves bounded per-task capacity and rotates between tasks", "[visual-decision]") {
    Gate gate;
    std::vector<std::string> order;
    VisualDecisionServiceImpl service(Options(), [&](const auto&, const auto& wire, const auto& jpeg, auto) {
        order.push_back(wire.at("task_id"));
        if (order.size() == 1) {
            gate.entered.set_value();
            gate.release.wait();
        }
        return Reply(wire, jpeg);
    });
    auto first =
        std::async(std::launch::async, [&] { return service.Decide(Request(), Run("a"), Image, 5s); });
    gate.entered.get_future().wait();
    auto second =
        std::async(std::launch::async, [&] { return service.Decide(Request(), Run("a"), Image, 5s); });
    const bool secondQueued = Queued(service, 1);
    auto rejected           = service.Decide(Request(), Run("a"), Image, 1500ms);
    auto other =
        std::async(std::launch::async, [&] { return service.Decide(Request(), Run("b"), Image, 5s); });
    const bool otherQueued = Queued(service, 2);
    auto full              = service.Decide(Request(), Run("c"), Image, 1500ms);
    gate.Open();
    REQUIRE(secondQueued);
    REQUIRE(otherQueued);
    REQUIRE(rejected.response["reason"] == "task_queue_full");
    REQUIRE(full.response["reason"] == "queue_full");
    REQUIRE(first.get().AllCompleted());
    REQUIRE(other.get().AllCompleted());
    REQUIRE(second.get().AllCompleted());
    REQUIRE(order == std::vector<std::string>{"a", "b", "a"});
}

TEST_CASE("Visual queued and running timeouts never publish late scores", "[visual-decision]") {
    Gate gate;
    std::atomic<int> calls{0}, queuedPrepares{0};
    VisualDecisionServiceImpl service(Options(), [&](const auto&, const auto& wire, const auto& jpeg, auto) {
        ++calls;
        gate.entered.set_value();
        gate.release.wait();
        return Reply(wire, jpeg);
    });
    auto first =
        std::async(std::launch::async, [&] { return service.Decide(Request(), Run("a"), Image, 100ms); });
    gate.entered.get_future().wait();
    auto queued = service.Decide(
        Request(), Run("b"),
        [&] {
            ++queuedPrepares;
            return Image();
        },
        20ms);
    auto expired = first.get();
    gate.Open();
    service.Stop();
    REQUIRE(queued.response["reason"] == "deadline_exceeded");
    REQUIRE(expired.response["reason"] == "deadline_exceeded");
    REQUIRE_FALSE(expired.response["items"][0].contains("probabilities"));
    REQUIRE(calls == 1);
    REQUIRE(queuedPrepares == 0);
    REQUIRE(service.Counters()["completed"] == 0);
}

TEST_CASE("Visual preparation consumes deadline and empty crops do not contact worker", "[visual-decision]") {
    const int mode = GENERATE(0, 1, 2);
    std::atomic<int> calls{0};
    VisualDecisionServiceImpl service(Options(), [&](const auto&, const auto& wire, const auto& jpeg, auto) {
        ++calls;
        return Reply(wire, jpeg);
    });
    auto result = service.Decide(
        Request(), Run(),
        [&] {
            if (mode == 1)
                return VisualDecisionImage{};
            if (mode == 2)
                throw std::runtime_error("crop failed");
            std::this_thread::sleep_for(35ms);
            return Image();
        },
        20ms);
    service.Stop();
    const std::vector<std::string> reasons{"deadline_exceeded", "invalid_roi_image", "roi_prepare_error"};
    REQUIRE(result.response["reason"] == reasons[mode]);
    REQUIRE(calls == 0);
}

TEST_CASE("Visual task epochs fence both in-flight work and publication after return", "[visual-decision]") {
    Gate gate;
    auto run = Run();
    VisualDecisionServiceImpl service(Options(), [&](const auto&, const auto& wire, const auto& jpeg, auto) {
        gate.entered.set_value();
        gate.release.wait();
        return Reply(wire, jpeg);
    });
    auto call = std::async(std::launch::async, [&] { return service.Decide(Request(), run, Image, 3s); });
    gate.entered.get_future().wait();
    run->Invalidate();
    gate.Open();
    REQUIRE(call.get().response["reason"] == "stale_task_run");
    bool published = false;
    REQUIRE_FALSE(run->CommitIfCurrent([&] { published = true; }));
    REQUIRE_FALSE(published);
    VisualDecisionServiceImpl fresh(Options(), Transport);
    auto next   = Run("task-1", "epoch-2");
    auto result = fresh.Decide(Request(), next, Image, 1500ms);
    REQUIRE(result.AllCompleted());
    REQUIRE(result.response["run_epoch"] == "epoch-2");
    next->Invalidate();
    REQUIRE_FALSE(next->CommitIfCurrent([&] { published = true; }));
}

TEST_CASE("Visual stop releases queued callers and rejects in-flight completion", "[visual-decision]") {
    Gate gate;
    VisualDecisionServiceImpl service(Options(), [&](const auto&, const auto& wire, const auto& jpeg, auto) {
        gate.entered.set_value();
        gate.release.wait();
        return Reply(wire, jpeg);
    });
    auto first =
        std::async(std::launch::async, [&] { return service.Decide(Request(), Run("a"), Image, 3s); });
    gate.entered.get_future().wait();
    auto second =
        std::async(std::launch::async, [&] { return service.Decide(Request(), Run("b"), Image, 3s); });
    const bool queued       = Queued(service, 1);
    auto stop               = std::async(std::launch::async, [&] { service.Stop(); });
    const auto secondResult = second.get();
    gate.Open();
    stop.get();
    REQUIRE(queued);
    REQUIRE(secondResult.response["reason"] == "service_stopped");
    REQUIRE(first.get().response["reason"] == "service_stopped");
    REQUIRE_FALSE(service.Available());
    REQUIRE(service.Decide(Request(), Run(), Image, 1500ms).response["reason"] == "service_stopped");
}

TEST_CASE("Visual IPC reads fragmented frames and rejects duplicate keys and wrong epochs",
          "[visual-decision][ipc]") {
    const int fault = GENERATE(0, 1, 2, 3);
    SocketPeer peer([&](int fd, const Json& request, const std::vector<uint8_t>& jpeg) {
        auto response = Reply(request, jpeg);
        if (fault == 2)
            response["run_epoch"] = "old-epoch";
        std::string encoded = response.dump();
        if (fault == 1)
            encoded.insert(1, "\"status\":\"completed\",");
        if (fault == 3) {
            uint32_t size = htonl(visual::kMaxJson + 1);
            send(fd, &size, sizeof(size), MSG_NOSIGNAL);
        } else {
            SocketPeer::Write(fd, encoded);
        }
    });
    auto options       = Options();
    options.socketPath = peer.path;
    VisualDecisionServiceImpl service(options);
    auto result = service.Decide(Request(), Run(), Image, 1500ms);
    peer.Check();
    REQUIRE(result.AllCompleted() == (fault == 0));
    REQUIRE(result.response["run_epoch"] == "epoch-1");
    if (fault == 1)
        REQUIRE(result.response["reason"] == "invalid_worker_response");
    if (fault == 3)
        REQUIRE(result.response["reason"] == "invalid_response_size");
}

TEST_CASE("Visual IPC deadline uses the same monotonic clock as the Python worker",
          "[visual-decision][ipc]") {
    int64_t remoteDeadline = 0;
    SocketPeer peer([&](int, const Json& request, const auto&) {
        remoteDeadline = request.at("deadline_monotonic_ms");
        std::this_thread::sleep_for(80ms);
    });
    auto options       = Options();
    options.socketPath = peer.path;
    VisualDecisionServiceImpl service(options);
    auto before = visual::MonotonicMilliseconds();
    auto result = service.Decide(Request(), Run(), Image, 40ms);
    peer.Check();
    REQUIRE(result.response["reason"] == "deadline_exceeded");
    REQUIRE(remoteDeadline >= before);
    REQUIRE(remoteDeadline <= before + 41);
}

TEST_CASE("Visual manifest loader pins schema and digest and JSON rejects ambiguity", "[visual-decision]") {
    REQUIRE(visual::Sha256(reinterpret_cast<const uint8_t*>("abc"), 3) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE_THROWS(visual::Parse("{\"a\":1,\"a\":2}"));
    REQUIRE_THROWS(visual::Parse("{\"a\":NaN}"));
    REQUIRE_THROWS(visual::Parse(std::string(30, '[') + "0" + std::string(30, ']')));
    const auto file = std::filesystem::temp_directory_path() / (cosmo::util::GenerateUUID() + ".json");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{file};
    Json manifest = {
        {"schema", 2}, {"qualification", "business-acceptance-pending"}, {"models", Json::object()}};
    for (const auto* role : {"tower", "adapter", "decision"})
        manifest["models"][role]["sha256"] = Options().release.modelHashes[role];
    auto encoded = manifest.dump();
    {
        std::ofstream output(file);
        output << encoded;
    }
    auto sha = visual::Sha256(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
    REQUIRE(visual::ReadRelease(file.string(), sha).Valid());
    REQUIRE_THROWS(visual::ReadRelease(file.string(), std::string(64, '0')));
    manifest["schema"] = 1;
    encoded            = manifest.dump();
    {
        std::ofstream output(file);
        output << encoded;
    }
    sha = visual::Sha256(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
    REQUIRE_THROWS(visual::ReadRelease(file.string(), sha));
}
