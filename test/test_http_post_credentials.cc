#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "LoopbackHttpServer.h"
#include "catch_amalgamated.hpp"
#include "network/http/HttpPost.h"
#include "util/CipherUtil.h"

namespace {

constexpr const char* kKeyFileEnv    = "COSMO_APP_KEY_FILE";
constexpr const char* kSecretFileEnv = "COSMO_APP_SECRET_FILE";

std::string HeaderValue(const std::string& request, const std::string& name) {
    const auto prefix = "\r\n" + name + ": ";
    const auto begin  = request.find(prefix);
    REQUIRE(begin != std::string::npos);
    const auto value_begin = begin + prefix.size();
    const auto end         = request.find("\r\n", value_begin);
    REQUIRE(end != std::string::npos);
    return request.substr(value_begin, end - value_begin);
}

class ScopedEnvironment {
public:
    ScopedEnvironment() {
        Save(kKeyFileEnv);
        Save(kSecretFileEnv);
        unsetenv(kKeyFileEnv);
        unsetenv(kSecretFileEnv);
    }

    ~ScopedEnvironment() {
        for (const auto& [key, value] : original_) {
            if (value) {
                setenv(key.c_str(), value->c_str(), 1);
            } else {
                unsetenv(key.c_str());
            }
        }
    }

    ScopedEnvironment(const ScopedEnvironment&)            = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    void Save(const char* key) {
        const char* value = std::getenv(key);
        original_.emplace_back(key, value ? std::optional<std::string>(value) : std::nullopt);
    }

    std::vector<std::pair<std::string, std::optional<std::string>>> original_;
};

class CredentialFiles {
public:
    CredentialFiles() {
        directory_ = std::filesystem::temp_directory_path() /
                     ("cosmo-http-post-credentials-" + std::to_string(getpid()));
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
        REQUIRE(std::filesystem::create_directories(directory_));
        key_path_    = directory_ / "app-key";
        secret_path_ = directory_ / "app-secret";
    }

    ~CredentialFiles() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    void WriteValid() const {
        Write(key_path_, std::string("application-") + "key\n");
        Write(secret_path_, std::string(32, 's') + "\n");
    }

    static void Write(const std::filesystem::path& path, const std::string& value) {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output.is_open());
        output << value;
        output.close();
        REQUIRE(output.good());
    }

    const std::filesystem::path& Key() const {
        return key_path_;
    }
    const std::filesystem::path& Secret() const {
        return secret_path_;
    }
    const std::filesystem::path& Directory() const {
        return directory_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path key_path_;
    std::filesystem::path secret_path_;
};

}  // namespace

TEST_CASE("HttpPost rejects signed requests without runtime credentials", "[HttpPost][security]") {
    ScopedEnvironment environment;
    cosmo::network::http::HttpPost post;
    post.SetIpPort("127.0.0.1:1");
    std::string response = "unchanged";

    REQUIRE_FALSE(post.HasAppInfo());
    REQUIRE_FALSE(post.GetFileServerConfig(response));
    CHECK(response == "unchanged");
}

TEST_CASE("HttpPost loads application credentials from mounted files", "[HttpPost][security]") {
    ScopedEnvironment environment;
    CredentialFiles files;
    files.WriteValid();
    REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
    REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);

    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    cosmo::network::http::HttpPost post;
    post.SetIpPort("127.0.0.1:" + std::to_string(server.Port()));
    const std::string body =
        R"({"resCode":1,"resData":{"fileServerUrl":"https://files.example.test","user":"test-user","token":"test-token"}})";
    std::vector<std::string> requests;
    auto served =
        std::async(std::launch::async, [&]() { return server.ServeResponses({{200, body}}, &requests); });
    std::string response;

    REQUIRE(post.HasAppInfo());
    REQUIRE(post.GetFileServerConfig(response));
    REQUIRE(served.get());
    CHECK(response == body);
    REQUIRE(requests.size() == 1);
    const auto& request = requests.front();
    CHECK(request.find("POST /adp-gtw/cwai/api/v1/manager/ai/getFileServerConfig HTTP/1.1\r\n") == 0);
    const auto body_begin = request.find("\r\n\r\n");
    REQUIRE(body_begin != std::string::npos);
    CHECK(request.substr(body_begin + 4) == "{}");
    CHECK(HeaderValue(request, "Content-Type") == "application/json");
    CHECK(HeaderValue(request, "AppKey") == "application-key");
    CHECK(HeaderValue(request, "RequestId").find("CWAI_Analyzer_") == 0);
    const auto nonce        = HeaderValue(request, "Nonce");
    const auto current_time = HeaderValue(request, "CurTime");
    CHECK_FALSE(nonce.empty());
    CHECK(current_time.size() == 13);
    CHECK(current_time.find_first_not_of("0123456789") == std::string::npos);
    CHECK(HeaderValue(request, "CheckSum") == cosmo::util::Sha1(nonce + std::string(32, 's') + current_time));
}

TEST_CASE("HttpPost rejects non-200 configuration responses", "[HttpPost][http]") {
    const int status = GENERATE(201, 401, 503);
    ScopedEnvironment environment;
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    cosmo::network::http::HttpPost post;
    post.SetIpPort("127.0.0.1:" + std::to_string(server.Port()));
    post.SetAppInfo("test-app-key", "test-app-secret");
    auto served          = std::async(std::launch::async,
                                      [&]() { return server.ServeResponses({{status, R"({"resCode":1})"}}); });
    std::string response = "unchanged";
    CHECK_FALSE(post.GetFileServerConfig(response));
    REQUIRE(served.get());
    CHECK(response == "unchanged");
}

TEST_CASE("HttpPost accepts credential files at the exact size boundary", "[HttpPost][security]") {
    ScopedEnvironment environment;
    CredentialFiles files;
    CredentialFiles::Write(files.Key(), std::string(4096, 'k'));
    CredentialFiles::Write(files.Secret(), std::string(4096, 's'));
    REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
    REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);

    cosmo::network::http::HttpPost post;
    REQUIRE(post.HasAppInfo());
}

TEST_CASE("HttpPost rejects partial or malformed credential mounts", "[HttpPost][security]") {
    ScopedEnvironment environment;
    CredentialFiles files;
    files.WriteValid();

    SECTION("only one file is configured") {
        REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }

    SECTION("credential file has multiple lines") {
        CredentialFiles::Write(files.Secret(), std::string(16, 'a') + "\n" + std::string(16, 'b'));
        REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
        REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }

    SECTION("credential file has repeated trailing line endings") {
        CredentialFiles::Write(files.Secret(), std::string(32, 's') + "\n\n");
        REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
        REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }

    SECTION("credential file contains a NUL byte") {
        CredentialFiles::Write(files.Secret(), std::string("secret\0suffix", 13));
        REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
        REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }

    SECTION("credential file exceeds the bounded read limit") {
        CredentialFiles::Write(files.Secret(), std::string(4097, 's'));
        REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
        REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }

    SECTION("credential file is empty after newline trimming") {
        CredentialFiles::Write(files.Secret(), "\r\n");
        REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
        REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }

    SECTION("credential path is relative") {
        REQUIRE(setenv(kKeyFileEnv, "relative-app-key", 1) == 0);
        REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }

    SECTION("credential path is not a regular file") {
        REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
        REQUIRE(setenv(kSecretFileEnv, files.Directory().c_str(), 1) == 0);
        cosmo::network::http::HttpPost post;
        REQUIRE_FALSE(post.HasAppInfo());
    }
}

TEST_CASE("HttpPost explicit credential injection is all-or-nothing", "[HttpPost][security]") {
    ScopedEnvironment environment;
    cosmo::network::http::HttpPost post;

    post.SetAppInfo(std::string("runtime-") + "key", std::string(32, 'r'));
    REQUIRE(post.HasAppInfo());

    post.SetAppInfo("", std::string(32, 'r'));
    REQUIRE_FALSE(post.HasAppInfo());
}
