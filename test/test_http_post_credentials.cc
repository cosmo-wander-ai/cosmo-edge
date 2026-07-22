#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "catch_amalgamated.hpp"
#include "network/http/HttpPost.h"
#include "network/http/HttpRequest.h"

namespace {

constexpr const char* kKeyFileEnv    = "COSMO_APP_KEY_FILE";
constexpr const char* kSecretFileEnv = "COSMO_APP_SECRET_FILE";

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
    cosmo::network::http::HttpStringHandler handler;
    cosmo::network::http::HttpRequest request("http://127.0.0.1/", handler);

    REQUIRE_FALSE(post.HasAppInfo());
    REQUIRE_FALSE(post.AppendHeaderS(request));
}

TEST_CASE("HttpPost loads application credentials from mounted files", "[HttpPost][security]") {
    ScopedEnvironment environment;
    CredentialFiles files;
    files.WriteValid();
    REQUIRE(setenv(kKeyFileEnv, files.Key().c_str(), 1) == 0);
    REQUIRE(setenv(kSecretFileEnv, files.Secret().c_str(), 1) == 0);

    cosmo::network::http::HttpPost post;
    cosmo::network::http::HttpStringHandler handler;
    cosmo::network::http::HttpRequest request("http://127.0.0.1/", handler);

    REQUIRE(post.HasAppInfo());
    REQUIRE(post.AppendHeaderS(request));
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
