#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "catch_amalgamated.hpp"
#include "network/http/HttpServer.h"
#include "network/http/HttpServerThreadPool.h"
#include "nlohmann/json.hpp"
#include "util/ErrorCode.h"
#include "util/IRequestDispatcher.h"
#include "util/MsgBaseTypes.h"

namespace cosmo::network::http {
namespace {

    using namespace std::chrono_literals;

    constexpr auto kShutdownOverlapDelay = 50ms;

    class BlockingDispatchState {
    public:
        void Enter(const cosmo::RequestDispatchContext& context, std::string body) {
            std::unique_lock<std::mutex> lock(mutex_);
            interface_           = context.uri;
            mtk_                 = context.credential;
            body_                = std::move(body);
            multipart_file_path_ = context.multipart_file_path;
            multipart_file_name_ = context.multipart_file_name;
            multipart_file_size_ = context.multipart_file_size;
            upload_id_           = context.upload_id;
            upload_offset_       = context.upload_offset;
            http_method_         = context.http_method;
            is_entered_          = true;
            ++entered_count_;
            condition_.notify_all();
            condition_.wait(lock, [this]() { return is_released_; });
        }

        bool WaitUntilEntered() {
            return WaitUntilEnteredCount(1);
        }

        bool WaitUntilEnteredCount(std::size_t count) {
            std::unique_lock<std::mutex> lock(mutex_);
            return condition_.wait_for(lock, 2s, [this, count]() { return entered_count_ >= count; });
        }

        bool Entered() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return is_entered_;
        }

        void Release() {
            std::lock_guard<std::mutex> lock(mutex_);
            is_released_ = true;
            condition_.notify_all();
        }

        std::string Interface() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return interface_;
        }

        std::string Mtk() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return mtk_;
        }

        std::string Body() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return body_;
        }

        std::string UploadId() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return upload_id_;
        }
        std::string UploadOffset() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return upload_offset_;
        }
        std::string HttpMethod() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return http_method_;
        }

        std::string MultipartFilePath() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return multipart_file_path_;
        }

        std::string MultipartFileName() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return multipart_file_name_;
        }

        std::uint64_t MultipartFileSize() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return multipart_file_size_;
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        bool is_entered_{false};
        bool is_released_{false};
        std::size_t entered_count_{0};
        std::string interface_;
        std::string mtk_;
        std::string body_;
        std::string multipart_file_path_;
        std::string multipart_file_name_;
        std::uint64_t multipart_file_size_{0};
        std::string upload_id_, upload_offset_, http_method_;
    };

    class BlockingDispatcher final : public cosmo::IRequestDispatcher {
    public:
        explicit BlockingDispatcher(std::shared_ptr<BlockingDispatchState> state)
            : state_(std::move(state)) {}

        bool SupportsRoute(const std::string& interface) override {
            return interface == "/test/request-lifetime";
        }

        cosmo::RequestAdmission InspectRequest(cosmo::RequestDispatchContext& context,
                                               bool require_known_route) override {
            if (context.credential == "rejected-token") {
                return cosmo::RequestAdmission::kUnauthorized;
            }
            if (require_known_route && !SupportsRoute(context.uri)) {
                return cosmo::RequestAdmission::kRouteNotFound;
            }
            context.principal = "test-user";
            return cosmo::RequestAdmission::kAllowed;
        }

        bool DispatchRequest(const cosmo::RequestDispatchContext& context, const std::string& body,
                             std::string& response) override {
            state_->Enter(context, body);
            response = R"({"ok":true})";
            return true;
        }

    private:
        std::shared_ptr<BlockingDispatchState> state_;
    };

    struct DispatcherProbe {
        std::atomic<int> live_count{0};
        std::atomic<int> dispatch_count{0};
        std::array<int, 5> dispatch_count_by_instance{};
    };

    class CountingDispatcher final : public cosmo::IRequestDispatcher {
    public:
        explicit CountingDispatcher(std::shared_ptr<DispatcherProbe> probe,
                                    std::optional<std::size_t> instance_index = std::nullopt)
            : probe_(std::move(probe)), instance_index_(instance_index) {
            probe_->live_count.fetch_add(1, std::memory_order_relaxed);
        }

        ~CountingDispatcher() override {
            probe_->live_count.fetch_sub(1, std::memory_order_relaxed);
        }

        bool SupportsRoute(const std::string& /*interface*/) override {
            return true;
        }

        cosmo::RequestAdmission InspectRequest(cosmo::RequestDispatchContext& context,
                                               bool /*require_known_route*/) override {
            context.principal = "thread-pool-test";
            return cosmo::RequestAdmission::kAllowed;
        }

        bool DispatchRequest(const cosmo::RequestDispatchContext& /*context*/, const std::string& /*body*/,
                             std::string& response) override {
            probe_->dispatch_count.fetch_add(1, std::memory_order_relaxed);
            if (instance_index_ && *instance_index_ < probe_->dispatch_count_by_instance.size()) {
                ++probe_->dispatch_count_by_instance[*instance_index_];
            }
            response = R"({"ok":true})";
            return true;
        }

    private:
        std::shared_ptr<DispatcherProbe> probe_;
        std::optional<std::size_t> instance_index_;
    };

    class CopyThrowingDispatcherFactory {
    public:
        CopyThrowingDispatcherFactory(std::shared_ptr<DispatcherProbe> probe,
                                      std::shared_ptr<std::atomic<bool>> should_throw)
            : probe_(std::move(probe)), should_throw_(std::move(should_throw)) {}

        CopyThrowingDispatcherFactory(const CopyThrowingDispatcherFactory& other)
            : probe_(other.probe_), should_throw_(other.should_throw_) {
            if (should_throw_->load(std::memory_order_relaxed)) {
                throw std::runtime_error("dispatcher factory copy failure");
            }
        }

        CopyThrowingDispatcherFactory(CopyThrowingDispatcherFactory&&) noexcept        = default;
        CopyThrowingDispatcherFactory& operator=(const CopyThrowingDispatcherFactory&) = delete;
        CopyThrowingDispatcherFactory& operator=(CopyThrowingDispatcherFactory&&)      = delete;

        std::unique_ptr<cosmo::IRequestDispatcher> operator()() const {
            return std::make_unique<CountingDispatcher>(probe_);
        }

    private:
        std::shared_ptr<DispatcherProbe> probe_;
        std::shared_ptr<std::atomic<bool>> should_throw_;
    };

    HttpServerThreadPool::DispatcherFactory MakeCountingDispatcherFactory(
        const std::shared_ptr<DispatcherProbe>& probe, std::size_t* call_count = nullptr) {
        return [probe, call_count]() -> std::unique_ptr<cosmo::IRequestDispatcher> {
            std::optional<std::size_t> instance_index;
            if (call_count != nullptr) {
                instance_index = (*call_count)++;
            }
            return std::make_unique<CountingDispatcher>(probe, instance_index);
        };
    }

    HttpServerCallbacks MakeThreadPoolTestCallbacks() {
        return {
            []() { return std::string("/tmp"); },
            []() { return std::string("/tmp"); },
            []() { return std::string("/tmp"); },
        };
    }

    cosmo::MsgEnvelope MakeHttpRequestMessage(std::string interface) {
        auto task          = std::make_unique<HttpReqTask>();
        task->request_time = std::chrono::steady_clock::now();
        task->interface    = std::move(interface);
        task->mtk          = "thread-pool-token";
        return {static_cast<int>(InnerMsgId::kHttpReq), std::move(task)};
    }

    class ReleaseGuard {
    public:
        explicit ReleaseGuard(std::shared_ptr<BlockingDispatchState> state) : state_(std::move(state)) {}

        ~ReleaseGuard() {
            state_->Release();
        }

        ReleaseGuard(const ReleaseGuard&)            = delete;
        ReleaseGuard& operator=(const ReleaseGuard&) = delete;

    private:
        std::shared_ptr<BlockingDispatchState> state_;
    };

    class HttpServerRunner {
    public:
        explicit HttpServerRunner(std::shared_ptr<BlockingDispatchState> state) : state_(std::move(state)) {}

        ~HttpServerRunner() {
            server_.UnInitialize();
            Join();
        }

        HttpServerRunner(const HttpServerRunner&)            = delete;
        HttpServerRunner& operator=(const HttpServerRunner&) = delete;

        bool Start(std::uint16_t port, std::string log_root = "/tmp") {
            HttpServerCallbacks callbacks{
                []() {
                    std::error_code error;
                    std::filesystem::create_directories("/tmp/cosmo-http-lifetime-test", error);
                    return error ? std::string{} : std::string("/tmp/cosmo-http-lifetime-test");
                },
                []() { return std::string("/tmp"); },
                [log_root = std::move(log_root)]() { return log_root; },
            };
            if (!server_.Initialize(
                    "127.0.0.1", port,
                    [state = state_]() { return std::make_unique<BlockingDispatcher>(state); },
                    std::move(callbacks))) {
                return false;
            }
            event_thread_ = std::thread([this]() {
                server_.DispatchMsg();
                {
                    std::lock_guard<std::mutex> lock(event_thread_mutex_);
                    event_thread_exited_ = true;
                }
                event_thread_condition_.notify_all();
            });
            return true;
        }

        HttpServer& Server() {
            return server_;
        }

        bool WaitUntilExited(std::chrono::milliseconds timeout) {
            std::unique_lock<std::mutex> lock(event_thread_mutex_);
            return event_thread_condition_.wait_for(lock, timeout, [this]() { return event_thread_exited_; });
        }

        void Join() {
            if (event_thread_.joinable()) {
                event_thread_.join();
            }
        }

    private:
        std::shared_ptr<BlockingDispatchState> state_;
        HttpServer server_;
        std::thread event_thread_;
        std::mutex event_thread_mutex_;
        std::condition_variable event_thread_condition_;
        bool event_thread_exited_{false};
    };

    class ScopedFd {
    public:
        explicit ScopedFd(int fd = -1) : fd_(fd) {}

        ~ScopedFd() {
            Reset();
        }

        ScopedFd(const ScopedFd&)            = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;

        ScopedFd(ScopedFd&& other) noexcept : fd_(other.fd_) {
            other.fd_ = -1;
        }

        ScopedFd& operator=(ScopedFd&& other) noexcept {
            if (this != &other) {
                Reset();
                fd_       = other.fd_;
                other.fd_ = -1;
            }
            return *this;
        }

        int Get() const {
            return fd_;
        }

        void Reset() {
            if (fd_ >= 0) {
                close(fd_);
                fd_ = -1;
            }
        }

    private:
        int fd_;
    };

    class ScopedSignalIgnore {
    public:
        using SignalHandler = void (*)(int);

        explicit ScopedSignalIgnore(int signal_number)
            : signal_number_(signal_number), previous_handler_(std::signal(signal_number, SIG_IGN)) {}

        ~ScopedSignalIgnore() {
            if (previous_handler_ != SIG_ERR) {
                std::signal(signal_number_, previous_handler_);
            }
        }

        ScopedSignalIgnore(const ScopedSignalIgnore&)            = delete;
        ScopedSignalIgnore& operator=(const ScopedSignalIgnore&) = delete;

    private:
        int signal_number_;
        SignalHandler previous_handler_;
    };

    std::uint16_t FindAvailablePort() {
        ScopedFd socket_fd(socket(AF_INET, SOCK_STREAM, 0));
        if (socket_fd.Get() < 0) {
            return 0;
        }

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;
        if (bind(socket_fd.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return 0;
        }

        socklen_t address_length = sizeof(address);
        if (getsockname(socket_fd.Get(), reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
            return 0;
        }
        return ntohs(address.sin_port);
    }

    ScopedFd Connect(std::uint16_t port) {
        ScopedFd socket_fd(socket(AF_INET, SOCK_STREAM, 0));
        if (socket_fd.Get() < 0) {
            return ScopedFd{};
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port   = htons(port);
        if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1 ||
            connect(socket_fd.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return ScopedFd{};
        }
        return socket_fd;
    }

    bool SendAll(int socket_fd, const std::string& data) {
        std::size_t sent_size{0};
        while (sent_size < data.size()) {
            auto result = send(socket_fd, data.data() + sent_size, data.size() - sent_size, 0);
            if (result <= 0) {
                return false;
            }
            sent_size += static_cast<std::size_t>(result);
        }
        return true;
    }

    std::string ReadAll(int socket_fd) {
        timeval timeout{};
        timeout.tv_sec = 2;
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        std::string response;
        char buffer[4096];
        while (true) {
            const auto size = recv(socket_fd, buffer, sizeof(buffer), 0);
            if (size <= 0) {
                break;
            }
            response.append(buffer, static_cast<size_t>(size));
        }
        return response;
    }

}  // namespace

TEST_CASE("HttpServerThreadPool rejects invalid initialization inputs",
          "[http-server][thread-pool][lifecycle]") {
    HttpServer server;
    HttpServerThreadPool thread_pool;
    auto probe               = std::make_shared<DispatcherProbe>();
    std::size_t call_count   = 0;
    const auto valid_factory = MakeCountingDispatcherFactory(probe, &call_count);

    for (const int thread_count : {-1, 0, 1, 2}) {
        CAPTURE(thread_count);
        CHECK_FALSE(thread_pool.Initialize(thread_count, &server, valid_factory));
    }
    CHECK(call_count == 0);
    CHECK_FALSE(thread_pool.Initialize(3, nullptr, valid_factory));
    CHECK(call_count == 0);
    CHECK_FALSE(thread_pool.Initialize(3, &server, HttpServerThreadPool::DispatcherFactory{}));
    CHECK(call_count == 0);

    CHECK_NOTHROW(thread_pool.Uninitialize());
    CHECK_NOTHROW(thread_pool.Uninitialize());
    CHECK(probe->live_count.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("HttpServerThreadPool rolls back dispatcher factory failures",
          "[http-server][thread-pool][lifecycle]") {
    for (const bool should_throw : {false, true}) {
        for (std::size_t failure_index = 0; failure_index < 3; ++failure_index) {
            CAPTURE(should_throw, failure_index);
            HttpServer server;
            HttpServerThreadPool thread_pool;
            auto probe             = std::make_shared<DispatcherProbe>();
            std::size_t call_count = 0;
            HttpServerThreadPool::DispatcherFactory failing_factory =
                [probe, &call_count, failure_index,
                 should_throw]() -> std::unique_ptr<cosmo::IRequestDispatcher> {
                const auto current_index = call_count++;
                if (current_index == failure_index) {
                    if (should_throw) {
                        throw std::runtime_error("dispatcher factory failure");
                    }
                    return nullptr;
                }
                return std::make_unique<CountingDispatcher>(probe);
            };

            CHECK_FALSE(thread_pool.Initialize(3, &server, std::move(failing_factory)));
            CHECK(call_count == failure_index + 1);
            CHECK(probe->live_count.load(std::memory_order_relaxed) == 0);
            auto rejected_message = MakeHttpRequestMessage("/normal");
            CHECK(thread_pool.PutMsg(std::move(rejected_message)) == -1);
            CHECK_NOTHROW(thread_pool.Uninitialize());

            REQUIRE(thread_pool.Initialize(3, &server, MakeCountingDispatcherFactory(probe)));
            CHECK(probe->live_count.load(std::memory_order_relaxed) == 3);
            thread_pool.Uninitialize();
            CHECK(probe->live_count.load(std::memory_order_relaxed) == 0);
        }
    }
}

TEST_CASE("HttpServerThreadPool preserves active workers and drains accepted requests",
          "[http-server][thread-pool][lifecycle]") {
    constexpr std::array<const char*, 5> kInterfaces = {"/normal", "/api/dologin", "/api/ResetSystem",
                                                        "/api/ThreadDebugInfo", "/api/QueryDeviceInfo"};

    for (const int thread_count : {3, 4}) {
        CAPTURE(thread_count);
        HttpServer server;
        HttpServerThreadPool thread_pool;
        auto probe             = std::make_shared<DispatcherProbe>();
        std::size_t call_count = 0;
        const auto factory     = MakeCountingDispatcherFactory(probe, &call_count);

        REQUIRE(thread_pool.Initialize(thread_count, &server, factory));
        CHECK(probe->live_count.load(std::memory_order_relaxed) == thread_count);
        CHECK(call_count == static_cast<std::size_t>(thread_count));

        CHECK_FALSE(thread_pool.Initialize(thread_count, &server, factory));
        CHECK(call_count == static_cast<std::size_t>(thread_count));

        for (const auto* interface : kInterfaces) {
            auto message = MakeHttpRequestMessage(interface);
            REQUIRE(thread_pool.PutMsg(std::move(message)) >= 0);
        }

        CHECK_NOTHROW(thread_pool.Uninitialize());
        CHECK(probe->dispatch_count.load(std::memory_order_relaxed) == static_cast<int>(kInterfaces.size()));
        CHECK(probe->dispatch_count_by_instance[0] == 2);
        CHECK(probe->dispatch_count_by_instance[1] == 2);
        CHECK(probe->dispatch_count_by_instance[2] == 1);
        if (thread_count == 4) {
            CHECK(probe->dispatch_count_by_instance[3] == 0);
        }
        CHECK(probe->live_count.load(std::memory_order_relaxed) == 0);
        CHECK_NOTHROW(thread_pool.Uninitialize());
    }
}

TEST_CASE("HttpServer releases resources after worker dispatcher construction fails",
          "[http-server][thread-pool][lifecycle]") {
    auto port = FindAvailablePort();
    REQUIRE(port != 0);

    HttpServer server;
    auto failing_probe                 = std::make_shared<DispatcherProbe>();
    std::size_t call_count             = 0;
    constexpr std::size_t kFailureCall = 2;
    HttpServer::DispatcherFactory failing_factory =
        [failing_probe, &call_count]() -> std::unique_ptr<cosmo::IRequestDispatcher> {
        const auto current_call = call_count++;
        if (current_call == kFailureCall) {
            throw std::runtime_error("worker dispatcher construction failure");
        }
        return std::make_unique<CountingDispatcher>(failing_probe);
    };

    CHECK_FALSE(
        server.Initialize("127.0.0.1", port, std::move(failing_factory), MakeThreadPoolTestCallbacks()));
    CHECK(call_count == kFailureCall + 1);
    CHECK(failing_probe->live_count.load(std::memory_order_relaxed) == 0);

    auto valid_probe = std::make_shared<DispatcherProbe>();
    REQUIRE(server.Initialize("127.0.0.1", port, MakeCountingDispatcherFactory(valid_probe),
                              MakeThreadPoolTestCallbacks()));
    CHECK(valid_probe->live_count.load(std::memory_order_relaxed) == 5);
    CHECK_NOTHROW(server.UnInitialize());
    CHECK(valid_probe->live_count.load(std::memory_order_relaxed) == 0);
    CHECK_NOTHROW(server.UnInitialize());
}

TEST_CASE("HttpServer releases resources when the worker dispatcher factory copy throws",
          "[http-server][thread-pool][lifecycle]") {
    auto port = FindAvailablePort();
    REQUIRE(port != 0);

    HttpServer server;
    auto failing_probe = std::make_shared<DispatcherProbe>();
    auto should_throw  = std::make_shared<std::atomic<bool>>(false);
    HttpServer::DispatcherFactory failing_factory =
        CopyThrowingDispatcherFactory(failing_probe, should_throw);
    should_throw->store(true, std::memory_order_relaxed);

    bool is_initialized = true;
    REQUIRE_NOTHROW(is_initialized = server.Initialize("127.0.0.1", port, std::move(failing_factory),
                                                       MakeThreadPoolTestCallbacks()));
    CHECK_FALSE(is_initialized);
    CHECK(failing_probe->live_count.load(std::memory_order_relaxed) == 0);

    auto valid_probe = std::make_shared<DispatcherProbe>();
    REQUIRE(server.Initialize("127.0.0.1", port, MakeCountingDispatcherFactory(valid_probe),
                              MakeThreadPoolTestCallbacks()));
    CHECK(valid_probe->live_count.load(std::memory_order_relaxed) == 5);
    CHECK_NOTHROW(server.UnInitialize());
    CHECK(valid_probe->live_count.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("HttpServer keeps libevent requests on the event thread during shutdown", "[http-server][thread]") {
    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);
    ReleaseGuard release_guard(state);

    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port));

    auto client = Connect(port);
    REQUIRE(client.Get() >= 0);

    const std::string body = R"({"value":7})";
    const std::string request =
        "POST /test/request-lifetime HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "mtk: lifetime-token\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "Connection: close\r\n\r\n" +
        body;
    REQUIRE(SendAll(client.Get(), request));
    REQUIRE(state->WaitUntilEntered());

    shutdown(client.Get(), SHUT_RDWR);
    client.Reset();

    std::thread stop_thread([&runner]() { runner.Server().UnInitialize(); });
    std::this_thread::sleep_for(kShutdownOverlapDelay);
    state->Release();
    stop_thread.join();
    runner.Join();

    CHECK(state->Interface() == "/test/request-lifetime");
    CHECK(state->Mtk() == "lifetime-token");
    CHECK(state->Body() == body);
}

TEST_CASE("HttpServer control-thread stop leaves cleanup on the event thread",
          "[http-server][lifecycle][thread]") {
    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);
    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port));

    auto client = Connect(port);
    REQUIRE(client.Get() >= 0);
    REQUIRE(SendAll(client.Get(),
                    "GET /unknown HTTP/1.1\r\n"
                    "Host: 127.0.0.1\r\n"
                    "mtk: test-token\r\n"
                    "Connection: close\r\n\r\n"));
    CHECK(ReadAll(client.Get()).find("400") != std::string::npos);

    REQUIRE_NOTHROW(runner.Server().RequestStop());
    REQUIRE_NOTHROW(runner.Server().RequestStop());
    const bool exited = runner.WaitUntilExited(2s);
    if (!exited) {
        runner.Server().UnInitialize();
    }
    runner.Join();
    REQUIRE(exited);
    REQUIRE_NOTHROW(runner.Server().UnInitialize());
    REQUIRE_NOTHROW(runner.Server().UnInitialize());
}

TEST_CASE("HttpServer log download rejects a symlink escape", "[http-server][security]") {
    namespace fs = std::filesystem;

    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    const auto suffix      = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path root    = fs::path("/tmp") / ("cosmo-http-log-root-" + suffix);
    const fs::path outside = fs::path("/tmp") / ("cosmo-http-log-outside-" + suffix + ".log");
    fs::create_directories(root);
    std::ofstream(outside) << "secret";
    fs::create_symlink(outside, root / "escape.log");

    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);
    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port, root.string()));

    auto client = Connect(port);
    REQUIRE(client.Get() >= 0);
    REQUIRE(SendAll(client.Get(),
                    "GET /logs/escape.log HTTP/1.1\r\n"
                    "Host: 127.0.0.1\r\n"
                    "mtk: test-token\r\n"
                    "Connection: close\r\n\r\n"));
    const auto response = ReadAll(client.Get());
    CHECK(response.find("404") != std::string::npos);
    CHECK(response.find("secret") == std::string::npos);

    runner.Server().UnInitialize();
    runner.Join();
    fs::remove_all(root);
    fs::remove(outside);
}

TEST_CASE("HttpServer streams single byte ranges without buffering the whole file",
          "[http-server][download][range]") {
    namespace fs = std::filesystem;

    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    const auto suffix   = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path root = fs::path("/tmp") / ("cosmo-http-range-root-" + suffix);
    fs::create_directories(root);
    std::ofstream(root / "range.log", std::ios::binary) << "0123456789";

    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);
    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port, root.string()));

    SECTION("bounded range returns 206 and only the requested bytes") {
        auto client = Connect(port);
        REQUIRE(client.Get() >= 0);
        REQUIRE(SendAll(client.Get(),
                        "GET /logs/range.log HTTP/1.1\r\n"
                        "Host: 127.0.0.1\r\n"
                        "mtk: test-token\r\n"
                        "Range: bytes=2-5\r\n"
                        "Connection: close\r\n\r\n"));
        const auto response = ReadAll(client.Get());
        CHECK(response.find("206 Partial Content") != std::string::npos);
        CHECK(response.find("Accept-Ranges: bytes") != std::string::npos);
        CHECK(response.find("Content-Range: bytes 2-5/10") != std::string::npos);
        CHECK(response.find("Content-Length: 4") != std::string::npos);
        const auto body_position = response.find("\r\n\r\n");
        REQUIRE(body_position != std::string::npos);
        CHECK(response.substr(body_position + 4) == "2345");
    }

    SECTION("suffix range returns the tail") {
        auto client = Connect(port);
        REQUIRE(client.Get() >= 0);
        REQUIRE(SendAll(client.Get(),
                        "GET /logs/range.log HTTP/1.1\r\n"
                        "Host: 127.0.0.1\r\n"
                        "mtk: test-token\r\n"
                        "Range: bytes=-3\r\n"
                        "Connection: close\r\n\r\n"));
        const auto response = ReadAll(client.Get());
        CHECK(response.find("206 Partial Content") != std::string::npos);
        CHECK(response.find("Content-Range: bytes 7-9/10") != std::string::npos);
        const auto body_position = response.find("\r\n\r\n");
        REQUIRE(body_position != std::string::npos);
        CHECK(response.substr(body_position + 4) == "789");
    }

    SECTION("unsatisfied range returns 416 with the current size") {
        auto client = Connect(port);
        REQUIRE(client.Get() >= 0);
        REQUIRE(SendAll(client.Get(),
                        "GET /logs/range.log HTTP/1.1\r\n"
                        "Host: 127.0.0.1\r\n"
                        "mtk: test-token\r\n"
                        "Range: bytes=99-\r\n"
                        "Connection: close\r\n\r\n"));
        const auto response = ReadAll(client.Get());
        CHECK(response.find("416 Range Not Satisfiable") != std::string::npos);
        CHECK(response.find("Content-Range: bytes */10") != std::string::npos);
    }

    runner.Server().UnInitialize();
    runner.Join();
    fs::remove_all(root);
}

TEST_CASE("HttpServer rejects unauthorized multipart before parsing or dispatch", "[http-server][security]") {
    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);

    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port));

    auto client = Connect(port);
    REQUIRE(client.Get() >= 0);

    const std::string body = "this is deliberately not multipart data";
    const std::string request =
        "POST /test/request-lifetime HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: multipart/form-data; boundary=test-boundary\r\n"
        "mtk: rejected-token\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "Connection: close\r\n\r\n" +
        body;
    REQUIRE(SendAll(client.Get(), request));

    const auto response = ReadAll(client.Get());
    CHECK(response.find("401") != std::string::npos);
    CHECK(response.find("Content-Type: application/json") != std::string::npos);
    const auto body_position = response.find("\r\n\r\n");
    REQUIRE(body_position != std::string::npos);
    const auto response_json = nlohmann::json::parse(response.substr(body_position + 4), nullptr, false);
    REQUIRE_FALSE(response_json.is_discarded());
    CHECK(response_json.at("resCode") == cosmo::kServerRspFailed);
    REQUIRE(response_json.at("resMsg").is_array());
    REQUIRE(response_json.at("resMsg").size() == 1);
    CHECK(response_json.at("resMsg").at(0).at("msgCode") ==
          std::to_string(static_cast<int>(cosmo::util::ErrorEnum::AuthFailed)));
    CHECK(response_json.at("resMsg").at(0).at("messageKey") == "api.error.AuthFailed");
    CHECK(response.find("rejected-token") == std::string::npos);
    CHECK_FALSE(state->Entered());

    runner.Server().UnInitialize();
    runner.Join();
}

TEST_CASE("HttpServer reports payload limits as 413 before dispatch", "[http-server][multipart][security]") {
    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);

    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port));

    SECTION("application JSON limit returns an actionable structured error") {
        auto client = Connect(port);
        REQUIRE(client.Get() >= 0);
        std::string body(1 * 1024 * 1024 + 1, 'x');
        const std::string headers =
            "POST /test/request-lifetime HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/json\r\n"
            "mtk: test-token\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
        REQUIRE(SendAll(client.Get(), headers));
        REQUIRE(SendAll(client.Get(), body));
        const auto response = ReadAll(client.Get());
        CHECK(response.find("413") != std::string::npos);
        const auto body_position = response.find("\r\n\r\n");
        REQUIRE(body_position != std::string::npos);
        const auto response_json = nlohmann::json::parse(response.substr(body_position + 4), nullptr, false);
        REQUIRE(response_json.is_object());
        REQUIRE(response_json.at("resMsg").is_array());
        CHECK(response_json.at("resMsg").at(0).at("messageKey") == "api.error.httpBodyTooLarge");
        CHECK(response_json.at("resMsg").at(0).at("details").at("limitBytes") == 1 * 1024 * 1024);
        CHECK(response_json.at("resMsg").at(0).at("recommendedAction") == "REDUCE_REQUEST_BODY");
        CHECK_FALSE(state->Entered());
    }

    SECTION("libevent keeps a finite emergency body backstop") {
        auto client = Connect(port);
        REQUIRE(client.Get() >= 0);
        constexpr std::size_t kClearlyAbusiveBody = 128 * 1024 * 1024;
        const std::string request =
            "POST /test/request-lifetime HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/json\r\n"
            "mtk: test-token\r\n"
            "Content-Length: " +
            std::to_string(kClearlyAbusiveBody) + "\r\nConnection: close\r\n\r\n";
        REQUIRE(SendAll(client.Get(), request));
        CHECK(ReadAll(client.Get()).find("413") != std::string::npos);
        CHECK_FALSE(state->Entered());
    }

    SECTION("multipart file limit preserves 413 below the transport limit") {
        auto client = Connect(port);
        REQUIRE(client.Get() >= 0);
        const std::string boundary = "oversized-file-boundary";
        std::string body           = "--" + boundary +
                           "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"large.bin\"\r\n"
                           "Content-Type: application/octet-stream\r\n\r\n";
        body.append(8 * 1024 * 1024 + 1, 'x');
        body += "\r\n--" + boundary + "--\r\n";
        const std::string headers =
            "POST /test/request-lifetime HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: multipart/form-data; boundary=" +
            boundary + "\r\nmtk: test-token\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\nConnection: close\r\n\r\n";
        REQUIRE(SendAll(client.Get(), headers));
        REQUIRE(SendAll(client.Get(), body));
        CHECK(ReadAll(client.Get()).find("413") != std::string::npos);
        CHECK_FALSE(state->Entered());
    }

    runner.Server().UnInitialize();
    runner.Join();
}

TEST_CASE("HttpServer passes server-only multipart provenance to the dispatcher",
          "[http-server][multipart][security]") {
    namespace fs = std::filesystem;

    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    const fs::path upload_root = "/tmp/cosmo-http-lifetime-test";
    std::error_code cleanup_error;
    fs::remove_all(upload_root, cleanup_error);
    REQUIRE(fs::create_directory(upload_root));

    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);
    ReleaseGuard release_guard(state);

    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port));

    const std::string boundary = "provenance-boundary";
    const std::string payload  = "server-owned-payload";
    const std::string body =
        "--" + boundary + "\r\nContent-Disposition: form-data; name=\"filePath\"\r\n\r\n/tmp/forged\r\n--" +
        boundary + "--\r\n";

    // Reserved metadata is rejected before dispatch and therefore cannot
    // manufacture provenance.
    auto rejected_client = Connect(port);
    REQUIRE(rejected_client.Get() >= 0);
    const std::string rejected_request =
        "POST /test/request-lifetime HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: multipart/form-data; boundary=" +
        boundary + "\r\nmtk: test-token\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n" + body;
    REQUIRE(SendAll(rejected_client.Get(), rejected_request));
    CHECK(ReadAll(rejected_client.Get()).find("400") != std::string::npos);
    CHECK_FALSE(state->Entered());

    const std::string valid_body =
        "--" + boundary + "\r\nContent-Disposition: form-data; name=\"purpose\"\r\n\r\nalgorithm\r\n--" +
        boundary +
        "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"../safe.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n" +
        payload + "\r\n--" + boundary + "--\r\n";
    auto client = Connect(port);
    REQUIRE(client.Get() >= 0);
    const std::string request =
        "POST /test/request-lifetime HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: multipart/form-data; boundary=" +
        boundary + "\r\nmtk: test-token\r\nContent-Length: " + std::to_string(valid_body.size()) +
        "\r\nConnection: close\r\n\r\n" + valid_body;
    REQUIRE(SendAll(client.Get(), request));
    REQUIRE(state->WaitUntilEntered());

    CHECK(state->MultipartFileName() == "safe.bin");
    CHECK(state->MultipartFileSize() == payload.size());
    REQUIRE_FALSE(state->MultipartFilePath().empty());
    CHECK(fs::path(state->MultipartFilePath()).parent_path() == upload_root);
    CHECK(fs::is_regular_file(state->MultipartFilePath()));
    CHECK(state->Body().find("/tmp/forged") == std::string::npos);

    state->Release();
    CHECK(ReadAll(client.Get()).find("200") != std::string::npos);
    runner.Server().UnInitialize();
    runner.Join();
    CHECK_FALSE(fs::exists(upload_root));
}

TEST_CASE("HttpServer bounds authenticated multipart spool reservations before writing more files",
          "[http-server][multipart][quota]") {
    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);
    ReleaseGuard release_guard(state);

    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port));

    const std::string boundary = "spool-quota-boundary";
    const std::string body     = "--" + boundary +
                             "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"chunk.bin\"\r\n"
                             "Content-Type: application/octet-stream\r\n\r\ndata\r\n--" +
                             boundary + "--\r\n";
    const std::string headers =
        "POST /test/request-lifetime HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: multipart/form-data; boundary=" +
        boundary + "\r\nmtk: test-token\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n";

    std::vector<ScopedFd> accepted_clients;
    for (std::size_t index = 0; index < 2; ++index) {
        auto client = Connect(port);
        REQUIRE(client.Get() >= 0);
        REQUIRE(SendAll(client.Get(), headers));
        REQUIRE(SendAll(client.Get(), body));
        accepted_clients.emplace_back(std::move(client));
    }
    REQUIRE(state->WaitUntilEnteredCount(2));

    auto rejected_client = Connect(port);
    REQUIRE(rejected_client.Get() >= 0);
    REQUIRE(SendAll(rejected_client.Get(), headers));
    REQUIRE(SendAll(rejected_client.Get(), body));
    CHECK(ReadAll(rejected_client.Get()).find("503") != std::string::npos);

    state->Release();
    for (auto& client : accepted_clients) {
        CHECK(ReadAll(client.Get()).find("200") != std::string::npos);
    }
    runner.Server().UnInitialize();
    runner.Join();
}

}  // namespace cosmo::network::http

namespace cosmo::network::http {
TEST_CASE("HttpServer preserves binary PUT bytes and upload headers", "[http-server][management]") {
    ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
    auto state = std::make_shared<BlockingDispatchState>();
    HttpServerRunner runner(state);
    ReleaseGuard release_guard(state);
    auto port = FindAvailablePort();
    REQUIRE(port != 0);
    REQUIRE(runner.Start(port));
    auto client = Connect(port);
    REQUIRE(client.Get() >= 0);
    const std::string payload("a\0b\xff", 4);
    const std::string request =
        "PUT /test/request-lifetime HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "Content-Type: application/octet-stream\r\nmtk: test-token\r\n"
        "X-Upload-Id: opaque-upload\r\nX-Upload-Offset: 1048576\r\n"
        "Content-Length: 4\r\nConnection: close\r\n\r\n" +
        payload;
    REQUIRE(SendAll(client.Get(), request));
    REQUIRE(state->WaitUntilEntered());
    CHECK(state->Body() == payload);
    CHECK(state->UploadId() == "opaque-upload");
    CHECK(state->UploadOffset() == "1048576");
    CHECK(state->HttpMethod() == "PUT");
    state->Release();
    CHECK(ReadAll(client.Get()).find("200") != std::string::npos);
}

}  // namespace cosmo::network::http
