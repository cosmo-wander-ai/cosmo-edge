#include "catch_amalgamated.hpp"
/*
 * test_file_service_impl.cc — FileServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Keep external platform services mocked, and use a loopback
 * HTTP server for deterministic transfer and upload lifecycle coverage.
 */
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "LoopbackHttpServer.h"
#include "media/EncodedImageInfo.h"
#include "network/http/HttpRequest.h"
#include "network/http/HttpRequestHandler.h"
#include "service/path/impl/FileServiceImpl.h"
#include "service/path/impl/file/HttpFileServerCliThread.h"
#include "support/ScopedPathOverride.h"
#include "util/FileUtil.h"
#include "util/PathUtil.h"

using namespace cosmo::service;

namespace {

class ScopedFileRemoval {
public:
    explicit ScopedFileRemoval(std::filesystem::path path) : path_(std::move(path)) {}

    ~ScopedFileRemoval() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    ScopedFileRemoval(const ScopedFileRemoval&)            = delete;
    ScopedFileRemoval& operator=(const ScopedFileRemoval&) = delete;

private:
    std::filesystem::path path_;
};

class ScopedDirectoryRemoval final {
public:
    explicit ScopedDirectoryRemoval(std::filesystem::path path) : path_(std::move(path)) {}

    ~ScopedDirectoryRemoval() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

struct UploadCompletion {
    std::string id;
    bool success;
    void* user;
};

class UploadCompletions final {
public:
    void Record(std::string id, bool success, void* user) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.push_back({std::move(id), success, user});
        changed_.notify_all();
    }

    HFSCallBacK Callback() {
        return [this](std::string id, bool success, void* user) { Record(std::move(id), success, user); };
    }

    bool WaitFor(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(5), [&]() { return values_.size() >= count; });
    }

    std::vector<UploadCompletion> Values() {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<UploadCompletion> values_;
};

std::string ServerUrl(const cosmo::test::LoopbackHttpServer& server) {
    return "http://127.0.0.1:" + std::to_string(server.Port());
}

class UploadWorker final {
public:
    explicit UploadWorker(const std::string& url                              = "",
                          std::function<void(std::string)> on_address_changed = {})
        : path_(std::filesystem::path("/tmp") / ("cosmo-upload-" + std::to_string(getpid()) + ".jpg")),
          cleanup_(path_),
          worker("file-upload-test", client, manager, std::move(on_address_changed)) {
        REQUIRE(cosmo::util::WriteFile(path_.string(), "upload-test-image-data"));
        client.SetIpPort(url);
        client.SetUserToken("old-user", "old-token");
        manager.SetAppInfo("test-app-key", "test-app-secret");
    }

    ~UploadWorker() {
        worker.Stop();
    }

    int Submit(const std::string& id, const HFSCallBacK& callback, void* user = nullptr,
               const std::string& file_url = "/remote/event.jpg") {
        return worker.Put(cosmo::MsgEnvelope(
            static_cast<int>(FileServerMsgId::kUploadFile),
            std::make_unique<CUploadFileTask>(id, callback, user, "test-bucket", file_url,
                                              cosmo::FMsgRspGetFileUrl{}, path_.string())));
    }

private:
    std::filesystem::path path_;
    ScopedFileRemoval cleanup_;

public:
    cosmo::network::http::HttpFileServerCli client;
    cosmo::network::http::HttpPost manager;
    CHttpFileServerCliThread worker;
};

std::string RefreshResponse(const std::string& url) {
    return "{\"resCode\":1,\"resData\":{\"fileServerUrl\":\"" + url +
           "\",\"user\":\"new-user\",\"token\":\"new-token\"}}";
}

bool FileContainsOnly(const std::filesystem::path& path, char expected) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytes = input.gcount();
        if (!std::all_of(buffer.begin(), buffer.begin() + bytes,
                         [expected](char value) { return value == expected; })) {
            return false;
        }
    }
    return input.eof();
}

}  // namespace

TEST_CASE("FileServiceImpl: construction and destruction", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut;
        // destructor runs Shutdown internally
    }());
}

TEST_CASE("FileServiceImpl: GetFileUrl returns empty when not initialized", "[FileService]") {
    FileServiceImpl sut;
    auto url = sut.GetFileUrl(FileType::Image);
    REQUIRE(url.empty());
}

TEST_CASE("FileServiceImpl: double destruction is safe", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut;
        // destructor calls Shutdown — verify no crash on double destroy
    }());
}

TEST_CASE("FileServiceImpl: GetFileUrl for different types", "[FileService]") {
    FileServiceImpl sut;

    SECTION("Image type returns empty when not initialized") {
        REQUIRE(sut.GetFileUrl(FileType::Image).empty());
    }

    SECTION("Video type returns empty when not initialized") {
        REQUIRE(sut.GetFileUrl(FileType::Video).empty());
    }
}

TEST_CASE("FileServiceImpl: multiple instances do not interfere", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut1;
        FileServiceImpl sut2;
    }());
}

TEST_CASE("FileServiceImpl: platform upload boundary rejects unmanaged files", "[FileService][consistency]") {
    const auto test_root = std::filesystem::path("/tmp") / ("cosmo-file-service-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    std::filesystem::create_directories(test_root, ec);
    REQUIRE_FALSE(ec);
    ScopedDirectoryRemoval cleanup(test_root);
    cosmo::test::ScopedPathOverride path_override(test_root.string(), test_root.string());

    const auto unmanaged = test_root.parent_path() / "unmanaged-platform-upload.jpg";
    REQUIRE(cosmo::util::WriteFile(unmanaged.string(), "not-an-image"));

    FileServiceImpl sut;
    std::atomic<int> callback_count{0};
    bool callback_result = true;
    sut.UploadFile(
        "task-1",
        [&](const std::string&, bool success, void*) {
            ++callback_count;
            callback_result = success;
        },
        nullptr, "jpg", unmanaged.string(), "gaf_commodity", "/remote/file.jpg");

    REQUIRE(callback_count.load() == 1);
    REQUIRE_FALSE(callback_result);
    std::filesystem::remove(unmanaged, ec);
    std::filesystem::remove_all(test_root, ec);
}

TEST_CASE("FileServiceImpl: accepted uploads always receive a terminal callback",
          "[FileService][consistency]") {
    const auto test_root =
        std::filesystem::path("/tmp") / ("cosmo-file-service-callback-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    std::filesystem::create_directories(test_root, ec);
    REQUIRE_FALSE(ec);
    ScopedDirectoryRemoval cleanup(test_root);
    cosmo::test::ScopedPathOverride path_override(test_root.string(), test_root.string());

    const auto local_file = std::filesystem::path(cosmo::path::GetRecordJsonPath()) / "event.jpg";
    REQUIRE(cosmo::util::WriteFile(local_file.string(), "image-data"));

    UploadCompletions completions;
    {
        FileServiceImpl sut;
        sut.UploadFile("task-2", completions.Callback(), nullptr, "jpg", local_file.string(), "gaf_commodity",
                       "/remote/file.jpg");
        REQUIRE(completions.WaitFor(1));
    }
    const auto results = completions.Values();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].id == "task-2");
    REQUIRE_FALSE(results[0].success);
}

TEST_CASE("FileServiceImpl: rejected upload callback may re-enter UploadFile",
          "[FileService][consistency][thread]") {
    const auto test_root =
        std::filesystem::path("/tmp") / ("cosmo-file-service-reentrant-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    ScopedDirectoryRemoval cleanup(test_root);
    cosmo::test::ScopedPathOverride path_override(test_root.string(), test_root.string());

    const auto local_file = std::filesystem::path(cosmo::path::GetRecordJsonPath()) / "event.jpg";
    REQUIRE(cosmo::util::WriteFile(local_file.string(), "image-data"));

    // A zero-capacity worker rejects Put synchronously.  Its failure callback
    // must run without FileService's worker mutex held.
    int outer_count   = 0;
    bool outer_result = true;
    int inner_count   = 0;
    bool inner_result = true;
    {
        FileServiceImpl sut(0);
        sut.UploadFile(
            "outer",
            [&](const std::string&, bool success, void*) {
                ++outer_count;
                outer_result = success;
                sut.UploadFile(
                    "inner",
                    [&](const std::string&, bool inner_success, void*) {
                        ++inner_count;
                        inner_result = inner_success;
                    },
                    nullptr, "jpg", local_file.string(), "gaf_commodity", "/remote/inner.jpg");
            },
            nullptr, "jpg", local_file.string(), "gaf_commodity", "/remote/outer.jpg");
    }

    REQUIRE(outer_count == 1);
    REQUIRE_FALSE(outer_result);
    REQUIRE(inner_count == 1);
    REQUIRE_FALSE(inner_result);
    std::filesystem::remove_all(test_root, ec);
}

TEST_CASE("File upload worker: success preserves request and terminal callback",
          "[FileService][http][upload]") {
    const int failures = GENERATE(0, 2);
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = ServerUrl(server);
    std::vector<cosmo::test::LoopbackHttpServer::Response> responses(failures, {200, R"({"resCode":0})"});
    responses.push_back({200, R"({"resCode":1})"});
    UploadCompletions completions;
    UploadWorker sut(url);
    std::vector<std::string> requests;
    auto served = std::async(std::launch::async, [&]() {
        return server.ServeResponses(responses, &requests,
                                     [&](std::size_t) { return completions.Values().empty(); });
    });
    int user    = 7;
    REQUIRE(sut.Submit("upload-id", completions.Callback(), &user) == 1);
    REQUIRE(sut.worker.start());
    REQUIRE(completions.WaitFor(1));
    sut.worker.Stop();

    REQUIRE(served.get());
    REQUIRE(requests.size() == static_cast<std::size_t>(failures + 1));
    for (const auto& request : requests) {
        CHECK(request.find("POST /file/auth/uploadFile HTTP/1.1\r\n") == 0);
        CHECK(request.find("\r\nbucket: test-bucket\r\n") != std::string::npos);
        CHECK(request.find("\r\nuser: old-user\r\n") != std::string::npos);
        CHECK(request.find("\r\ntoken: old-token\r\n") != std::string::npos);
        CHECK(request.find("\r\nisHttps: true\r\n") != std::string::npos);
        CHECK(request.find("\r\nfileUrl: " + url + "/remote/event.jpg\r\n") != std::string::npos);
        CHECK(request.find("upload-test-image-data") != std::string::npos);
        CHECK(request.find("name=\"file\"; filename=\"") != std::string::npos);
    }
    const auto results = completions.Values();
    REQUIRE(results.size() == 1);
    CHECK(results[0].id == "upload-id");
    CHECK(results[0].success);
    CHECK(results[0].user == &user);
}

TEST_CASE("File upload worker: failures exhaust three attempts before notifying",
          "[FileService][http][upload]") {
    const auto failure = GENERATE(0, 1, 2);
    const cosmo::test::LoopbackHttpServer::Response response =
        failure == 0 ? cosmo::test::LoopbackHttpServer::Response{503, "unavailable"}
                     : cosmo::test::LoopbackHttpServer::Response{
                           200, failure == 1 ? R"({"resCode":0})" : "not-json-response"};
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    UploadCompletions completions;
    UploadWorker sut(ServerUrl(server));
    std::vector<std::string> requests;
    auto served = std::async(std::launch::async, [&]() {
        // A fourth attempt would succeed, making an excessive retry observable.
        return server.ServeResponses({response, response, response, {200, R"({"resCode":1})"}}, &requests,
                                     [&](std::size_t) { return completions.Values().empty(); });
    });
    REQUIRE(sut.Submit("failed", completions.Callback()) == 1);
    REQUIRE(sut.worker.start());
    REQUIRE(completions.WaitFor(1));
    sut.worker.Stop();
    server.StopServing();

    REQUIRE_FALSE(served.get());
    CHECK(requests.size() == 3);
    const auto results = completions.Values();
    REQUIRE(results.size() == 1);
    CHECK(results[0].id == "failed");
    CHECK_FALSE(results[0].success);
}

TEST_CASE("File upload worker: expired credentials refresh without corrupting file URLs",
          "[FileService][http][upload]") {
    const std::string code = GENERATE("400", "180000010", "180000009");
    const bool absolute    = GENERATE(false, true);
    cosmo::test::LoopbackHttpServer old_server;
    cosmo::test::LoopbackHttpServer new_server;
    REQUIRE(old_server.Start());
    REQUIRE(new_server.Start());
    const auto old_url = ServerUrl(old_server);
    const auto new_url = ServerUrl(new_server);
    const std::string file_url =
        absolute ? "https://files.example.test/remote/event.jpg" : "/remote/event.jpg";
    UploadCompletions completions;
    std::vector<std::string> changed_addresses;
    UploadWorker sut(old_url, [&](std::string address) { changed_addresses.push_back(std::move(address)); });
    sut.manager.SetIpPort("127.0.0.1:" + std::to_string(old_server.Port()));
    std::vector<std::string> old_requests;
    std::vector<std::string> new_requests;
    auto old_served = std::async(std::launch::async, [&]() {
        return old_server.ServeResponses(
            {{200, "{\"resCode\":0,\"resMsg\":[{\"msgCode\":\"" + code + "\"}]}"},
             {200, RefreshResponse(new_url)}},
            &old_requests);
    });
    auto new_served = std::async(std::launch::async, [&]() {
        return new_server.ServeResponses({{200, R"({"resCode":1})"}}, &new_requests);
    });
    REQUIRE(sut.Submit("refresh", completions.Callback(), nullptr, file_url) == 1);
    REQUIRE(sut.worker.start());
    REQUIRE(completions.WaitFor(1));
    sut.worker.Stop();

    REQUIRE(old_served.get());
    REQUIRE(new_served.get());
    REQUIRE(old_requests.size() == 2);
    REQUIRE(new_requests.size() == 1);
    CHECK(old_requests[1].find("POST /adp-gtw/cwai/api/v1/manager/ai/getFileServerConfig HTTP/1.1") == 0);
    CHECK(new_requests[0].find("POST /file/auth/uploadFile HTTP/1.1") == 0);
    CHECK(new_requests[0].find("\r\nuser: new-user\r\n") != std::string::npos);
    CHECK(new_requests[0].find("\r\ntoken: new-token\r\n") != std::string::npos);
    CHECK(old_requests[0].find("\r\nfileUrl: " + (absolute ? file_url : old_url + file_url) + "\r\n") !=
          std::string::npos);
    CHECK(new_requests[0].find("\r\nfileUrl: " + (absolute ? file_url : new_url + file_url) + "\r\n") !=
          std::string::npos);
    CHECK(changed_addresses == std::vector<std::string>{new_url});
    const auto results = completions.Values();
    REQUIRE(results.size() == 1);
    CHECK(results[0].success);
}

TEST_CASE("File upload worker: failed configuration refresh preserves credentials and notifies once",
          "[FileService][http][upload]") {
    const auto failure = GENERATE(0, 1, 2);
    const cosmo::test::LoopbackHttpServer::Response refresh =
        failure == 0 ? cosmo::test::LoopbackHttpServer::Response{503, "unavailable"}
                     : cosmo::test::LoopbackHttpServer::Response{
                           200, failure == 1 ? "not-json-response" : R"({"resCode":0})"};
    const cosmo::test::LoopbackHttpServer::Response expired{
        200, R"({"resCode":0,"resMsg":[{"msgCode":"180000010"}]})"};
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = ServerUrl(server);
    UploadCompletions completions;
    std::vector<std::string> changed_addresses;
    UploadWorker sut(url, [&](std::string address) { changed_addresses.push_back(std::move(address)); });
    sut.manager.SetIpPort("127.0.0.1:" + std::to_string(server.Port()));
    std::vector<std::string> requests;
    auto served = std::async(std::launch::async, [&]() {
        return server.ServeResponses({expired, refresh, expired, refresh, expired, refresh}, &requests,
                                     [&](std::size_t) { return completions.Values().empty(); });
    });
    REQUIRE(sut.Submit("failed-refresh", completions.Callback()) == 1);
    REQUIRE(sut.worker.start());
    REQUIRE(completions.WaitFor(1));
    sut.worker.Stop();

    REQUIRE(served.get());
    REQUIRE(requests.size() == 6);
    for (std::size_t index = 0; index < requests.size(); index += 2) {
        CHECK(requests[index].find("\r\nuser: old-user\r\n") != std::string::npos);
        CHECK(requests[index].find("\r\ntoken: old-token\r\n") != std::string::npos);
        CHECK(requests[index].find("\r\nfileUrl: " + url + "/remote/event.jpg\r\n") != std::string::npos);
    }
    CHECK(changed_addresses.empty());
    const auto results = completions.Values();
    REQUIRE(results.size() == 1);
    CHECK(results[0].id == "failed-refresh");
    CHECK_FALSE(results[0].success);
}

TEST_CASE("File upload worker: processing exceptions fail once and allow the next task",
          "[FileService][http][upload]") {
    const bool standard_exception = GENERATE(false, true);
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = ServerUrl(server);
    UploadCompletions completions;
    UploadWorker sut(url, [&](std::string) {
        if (standard_exception) {
            throw std::runtime_error("address callback failed");
        }
        throw 7;
    });
    sut.manager.SetIpPort("127.0.0.1:" + std::to_string(server.Port()));
    auto served = std::async(std::launch::async, [&]() {
        return server.ServeResponses({{200, R"({"resCode":0,"resMsg":[{"msgCode":"400"}]})"},
                                      {200, RefreshResponse(url)},
                                      {200, R"({"resCode":1})"}});
    });
    REQUIRE(sut.Submit("throws", completions.Callback()) == 1);
    REQUIRE(sut.Submit("next", completions.Callback()) == 2);
    REQUIRE(sut.worker.start());
    REQUIRE(completions.WaitFor(2));
    sut.worker.Stop();

    REQUIRE(served.get());
    const auto results = completions.Values();
    REQUIRE(results.size() == 2);
    CHECK(results[0].id == "throws");
    CHECK_FALSE(results[0].success);
    CHECK(results[1].id == "next");
    CHECK(results[1].success);
}

TEST_CASE("File upload worker: throwing terminal callbacks are not repeated", "[FileService][http][upload]") {
    const bool standard_exception = GENERATE(false, true);
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    UploadCompletions completions;
    UploadWorker sut(ServerUrl(server));
    auto served = std::async(std::launch::async, [&]() {
        return server.ServeResponses({{200, R"({"resCode":1})"}, {200, R"({"resCode":1})"}});
    });
    REQUIRE(sut.Submit("throws", [&](std::string id, bool success, void* user) {
        completions.Record(std::move(id), success, user);
        if (standard_exception) {
            throw std::runtime_error("terminal callback failed");
        }
        throw 7;
    }) == 1);
    REQUIRE(sut.Submit("next", completions.Callback()) == 2);
    REQUIRE(sut.worker.start());
    REQUIRE(completions.WaitFor(2));
    sut.worker.Stop();

    REQUIRE(served.get());
    const auto results = completions.Values();
    REQUIRE(results.size() == 2);
    CHECK(results[0].id == "throws");
    CHECK(results[0].success);
    CHECK(results[1].id == "next");
    CHECK(results[1].success);
}

TEST_CASE("File upload worker: stop cancels queued tasks once and rejects later submissions",
          "[FileService][upload][thread]") {
    UploadCompletions completions;
    UploadWorker sut;
    int first_user  = 1;
    int second_user = 2;
    // Completion belongs to each submission, even when task IDs are identical.
    REQUIRE(sut.Submit("same-id", completions.Callback(), &first_user) == 1);
    REQUIRE(sut.Submit("same-id", completions.Callback(), &second_user) == 2);
    sut.worker.Stop();
    CHECK(sut.worker.MsgSize() == 0);
    sut.worker.Stop();
    REQUIRE(sut.Submit("after-stop", completions.Callback()) == -1);
    sut.worker.Stop();

    const auto results = completions.Values();
    REQUIRE(results.size() == 3);
    CHECK(results[0].user == &first_user);
    CHECK(results[1].user == &second_user);
    CHECK(results[2].id == "after-stop");
    for (const auto& result : results) {
        CHECK_FALSE(result.success);
    }
    CHECK(sut.worker.MsgSize() == 0);
}

TEST_CASE("File upload worker: stop completes an in-flight upload and cancels pending work",
          "[FileService][http][upload][thread]") {
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    UploadCompletions completions;
    UploadWorker sut(ServerUrl(server));
    std::promise<void> request_received;
    auto received = request_received.get_future();
    std::promise<void> release_response;
    auto release = release_response.get_future();
    auto served  = std::async(std::launch::async, [&]() {
        return server.ServeResponses({{200, R"({"resCode":1})"}}, nullptr, [&](std::size_t) {
            request_received.set_value();
            return release.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        });
    });
    REQUIRE(sut.Submit("in-flight", completions.Callback()) == 1);
    REQUIRE(sut.worker.start());
    REQUIRE(received.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(sut.Submit("pending", completions.Callback()) == 1);
    auto stopped = std::async(std::launch::async, [&]() { sut.worker.Stop(); });
    // The stop message proves Stop has reached the join while HTTP remains gated.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (sut.worker.MsgSize() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    CHECK(sut.worker.MsgSize() == 2);
    CHECK(completions.Values().empty());
    release_response.set_value();
    REQUIRE(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    stopped.get();

    REQUIRE(served.get());
    const auto results = completions.Values();
    REQUIRE(results.size() == 2);
    CHECK(results[0].id == "in-flight");
    CHECK(results[0].success);
    CHECK(results[1].id == "pending");
    CHECK_FALSE(results[1].success);
    CHECK(sut.worker.MsgSize() == 0);
}

TEST_CASE("HttpStringHandler: response size limit aborts before overflow", "[FileService][boundary]") {
    cosmo::network::http::HttpStringHandler handler(4);
    REQUIRE(handler.AppendData("data", 4) == 4);
    REQUIRE(handler.AppendData("x", 1) == 0);
    REQUIRE(handler.GetData() == "data");
}

TEST_CASE("FileServiceImpl: download rejects non-HTTP URLs and clears stale output",
          "[FileService][boundary]") {
    FileServiceImpl sut;
    std::vector<uint8_t> data{1, 2, 3};
    REQUIRE_FALSE(sut.DownloadFile("file:///etc/passwd", data));
    REQUIRE(data.empty());
}

TEST_CASE("FileServiceImpl: image download budget is monotonic at the memory reserve",
          "[FileService][boundary]") {
    constexpr std::uint64_t kTotalBytes   = 2ULL * 1024 * 1024 * 1024;
    constexpr std::uint64_t kReserveBytes = 256ULL * 1024 * 1024;
    constexpr std::uint64_t kBodySize     = 17ULL * 1024 * 1024 + 123;

    CHECK(cosmo::service::detail::CalculateImageDownloadBudgetBytes(kTotalBytes, kReserveBytes - 1) == 0);
    CHECK(cosmo::service::detail::CalculateImageDownloadBudgetBytes(kTotalBytes, kReserveBytes) == 0);
    CHECK(cosmo::service::detail::CalculateImageDownloadBudgetBytes(kTotalBytes, kReserveBytes + kBodySize) ==
          kBodySize);
    CHECK(cosmo::service::detail::CalculateImageDownloadBudgetBytes(kTotalBytes, kTotalBytes) ==
          static_cast<std::size_t>(cosmo::media::kVideoFrameMaxSize));
}

TEST_CASE("FileServiceImpl: image download budget honors percentage reserve", "[FileService][boundary]") {
    constexpr std::uint64_t kTotalBytes      = 4ULL * 1024 * 1024 * 1024;
    constexpr std::uint64_t kReserveBytes    = kTotalBytes / 10;
    constexpr std::uint64_t kAvailableBeyond = 8ULL * 1024 * 1024;

    CHECK(cosmo::service::detail::CalculateImageDownloadBudgetBytes(kTotalBytes, kReserveBytes) == 0);
    CHECK(cosmo::service::detail::CalculateImageDownloadBudgetBytes(
              kTotalBytes, kReserveBytes + kAvailableBeyond) == kAvailableBeyond);
}

TEST_CASE("FileServiceImpl: resource budget permits HTTP images beyond the legacy 16 MiB cap",
          "[FileService][http][boundary]") {
    constexpr std::size_t kBodySize = 17U * 1024 * 1024 + 123;
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/large-image";

    std::atomic<bool> served{false};
    std::thread server_thread(
        [&]() { served.store(server.ServeOnce(kBodySize, 'I'), std::memory_order_release); });
    FileServiceImpl sut;
    std::vector<std::uint8_t> data;
    const bool downloaded = sut.DownloadFile(url, data);
    server_thread.join();

    REQUIRE(served.load(std::memory_order_acquire));
    REQUIRE(downloaded);
    REQUIRE(data.size() == kBodySize);
    REQUIRE(data.front() == static_cast<std::uint8_t>('I'));
    REQUIRE(data.back() == static_cast<std::uint8_t>('I'));
}

TEST_CASE("HttpFileHandler: HTTP video-sized responses stream to disk beyond 16 MiB",
          "[FileService][http][streaming]") {
    constexpr std::size_t kBodySize = 32U * 1024 * 1024 + 321;
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/large-video";

    const auto output =
        std::filesystem::path("/tmp") / ("cosmo-http-video-" + std::to_string(getpid()) + ".mp4");
    [[maybe_unused]] ScopedFileRemoval output_cleanup(output);
    std::error_code error;
    std::filesystem::remove(output, error);
    std::atomic<bool> served{false};
    std::thread server_thread(
        [&]() { served.store(server.ServeOnce(kBodySize, 'V'), std::memory_order_release); });

    cosmo::network::http::HttpFileHandler handler(output.string());
    cosmo::network::http::HttpRequest request(url, &handler);
    request.SetTimeout(30);
    const auto status = request.Submit(cosmo::network::http::HttpRequestMethod::kGet);
    server_thread.join();

    REQUIRE(served.load(std::memory_order_acquire));
    REQUIRE(static_cast<int>(status) == 200);
    REQUIRE(std::filesystem::file_size(output, error) == kBodySize);
    REQUIRE_FALSE(error);
    REQUIRE(FileContainsOnly(output, 'V'));
}

TEST_CASE("HttpFileHandler: unopened destination aborts the HTTP transfer", "[FileService][http][boundary]") {
    const auto missing_parent =
        std::filesystem::path("/tmp") / ("cosmo-http-missing-parent-" + std::to_string(getpid()));
    const auto output = missing_parent / "response.bin";
    std::error_code error;
    std::filesystem::remove_all(missing_parent, error);
    REQUIRE_FALSE(std::filesystem::exists(missing_parent));

    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/write-failure";
    std::atomic<bool> served{false};
    std::thread server_thread([&]() { served.store(server.ServeOnce(1, 'F'), std::memory_order_release); });

    cosmo::network::http::HttpFileHandler handler(output.string());
    cosmo::network::http::HttpRequest request(url, &handler);
    const auto status = request.Submit(cosmo::network::http::HttpRequestMethod::kGet);
    server_thread.join();

    REQUIRE(served.load(std::memory_order_acquire));
    REQUIRE(status == -1);
    REQUIRE_FALSE(std::filesystem::exists(output));
}

TEST_CASE("HttpFileHandler: final flush failure rejects an HTTP 200 response",
          "[FileService][http][boundary]") {
    REQUIRE(std::filesystem::exists("/dev/full"));

    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/flush-failure";
    std::atomic<bool> served{false};
    std::thread server_thread([&]() { served.store(server.ServeOnce(1, 'F'), std::memory_order_release); });

    cosmo::network::http::HttpFileHandler handler("/dev/full");
    cosmo::network::http::HttpRequest request(url, &handler);
    const auto status = request.Submit(cosmo::network::http::HttpRequestMethod::kGet);
    server_thread.join();

    REQUIRE(served.load(std::memory_order_acquire));
    REQUIRE(status == -1);
}
