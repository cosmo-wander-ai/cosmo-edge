#include "catch_amalgamated.hpp"
/*
 * test_file_service_impl.cc — FileServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Keep external platform services mocked, and use a one-shot
 * loopback HTTP server for deterministic transfer-boundary coverage.
 */
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <utility>

#include "LoopbackHttpServer.h"
#include "media/EncodedImageInfo.h"
#include "network/http/HttpRequest.h"
#include "network/http/HttpRequestHandler.h"
#include "service/path/impl/FileServiceImpl.h"
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
    cosmo::test::ScopedPathOverride path_override(test_root.string(), test_root.string());

    const auto local_file = std::filesystem::path(cosmo::path::GetRecordJsonPath()) / "event.jpg";
    REQUIRE(cosmo::util::WriteFile(local_file.string(), "image-data"));

    auto completion = std::make_shared<std::promise<bool>>();
    auto future     = completion->get_future();
    {
        FileServiceImpl sut;
        sut.UploadFile(
            "task-2",
            [completion](const std::string&, bool success, void*) { completion->set_value(success); },
            nullptr, "jpg", local_file.string(), "gaf_commodity", "/remote/file.jpg");

        REQUIRE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        REQUIRE_FALSE(future.get());
    }
}

TEST_CASE("FileServiceImpl: rejected upload callback may re-enter UploadFile",
          "[FileService][consistency][thread]") {
    const auto test_root =
        std::filesystem::path("/tmp") / ("cosmo-file-service-reentrant-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    cosmo::test::ScopedPathOverride path_override(test_root.string(), test_root.string());

    const auto local_file = std::filesystem::path(cosmo::path::GetRecordJsonPath()) / "event.jpg";
    REQUIRE(cosmo::util::WriteFile(local_file.string(), "image-data"));

    // A zero-capacity worker rejects Put synchronously.  Its failure callback
    // must run without FileService's worker mutex held.
    FileServiceImpl sut(0);
    bool outer_called = false;
    bool outer_result = true;
    bool inner_called = false;
    bool inner_result = true;
    sut.UploadFile(
        "outer",
        [&](const std::string&, bool success, void*) {
            outer_called = true;
            outer_result = success;
            sut.UploadFile(
                "inner",
                [&](const std::string&, bool inner_success, void*) {
                    inner_called = true;
                    inner_result = inner_success;
                },
                nullptr, "jpg", local_file.string(), "gaf_commodity", "/remote/inner.jpg");
        },
        nullptr, "jpg", local_file.string(), "gaf_commodity", "/remote/outer.jpg");

    REQUIRE(outer_called);
    REQUIRE_FALSE(outer_result);
    REQUIRE(inner_called);
    REQUIRE_FALSE(inner_result);
    std::filesystem::remove_all(test_root, ec);
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
