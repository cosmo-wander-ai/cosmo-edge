// HttpPost — Http Post implementation.

#include "network/http/HttpPost.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

#include "network/http/HttpRequest.h"
#include "util/CipherUtil.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo::network::http {

namespace {

    constexpr const char* kAppKeyFileEnv    = "COSMO_APP_KEY_FILE";
    constexpr const char* kAppSecretFileEnv = "COSMO_APP_SECRET_FILE";
    constexpr size_t kMaxCredentialBytes    = 4096;

    enum class CredentialLoadState { kMissing, kLoaded, kInvalid };

    CredentialLoadState LoadCredentialFile(const char* env_name, std::string& value) {
        const char* configured_path = std::getenv(env_name);
        if (!configured_path || configured_path[0] == '\0') {
            return CredentialLoadState::kMissing;
        }

        const std::string path(configured_path);
        if (path.empty() || path.front() != '/') {
            LOG_ERRO("{} must reference an absolute credential file", env_name);
            return CredentialLoadState::kInvalid;
        }

        std::error_code path_error;
        if (!std::filesystem::is_regular_file(path, path_error) || path_error) {
            LOG_ERRO("{} credential path must reference a regular file", env_name);
            return CredentialLoadState::kInvalid;
        }
        path_error.clear();
        const auto raw_size = std::filesystem::file_size(path, path_error);
        if (path_error || raw_size == 0 || raw_size > kMaxCredentialBytes) {
            LOG_ERRO("{} credential file is empty, invalid or exceeds {} bytes", env_name,
                     kMaxCredentialBytes);
            return CredentialLoadState::kInvalid;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            LOG_ERRO("{} credential file is not readable", env_name);
            return CredentialLoadState::kInvalid;
        }

        std::array<char, kMaxCredentialBytes + 1> buffer{};
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytes_read = input.gcount();
        if (input.bad() || bytes_read < 0 || static_cast<size_t>(bytes_read) > kMaxCredentialBytes) {
            LOG_ERRO("{} credential file is invalid or exceeds {} bytes", env_name, kMaxCredentialBytes);
            return CredentialLoadState::kInvalid;
        }
        std::string loaded(buffer.data(), static_cast<size_t>(bytes_read));
        if (!loaded.empty() && loaded.back() == '\n') {
            loaded.pop_back();
            if (!loaded.empty() && loaded.back() == '\r') {
                loaded.pop_back();
            }
        } else if (!loaded.empty() && loaded.back() == '\r') {
            loaded.pop_back();
        }
        if (loaded.empty() || loaded.find('\0') != std::string::npos ||
            loaded.find('\n') != std::string::npos || loaded.find('\r') != std::string::npos) {
            LOG_ERRO("{} credential file must contain one non-empty line", env_name);
            return CredentialLoadState::kInvalid;
        }

        value = std::move(loaded);
        return CredentialLoadState::kLoaded;
    }

    const char* CredentialStateName(CredentialLoadState state) {
        switch (state) {
            case CredentialLoadState::kMissing:
                return "missing";
            case CredentialLoadState::kLoaded:
                return "loaded";
            case CredentialLoadState::kInvalid:
                return "invalid";
        }
        return "unknown";
    }

}  // namespace

HttpPost::HttpPost() {
    LoadAppInfoFromRuntime();
}

void HttpPost::SetIpPort(const std::string& ip_port) {
    get_file_server_config_url_ = "http://" + ip_port + "/adp-gtw/cwai/api/v1/manager/ai/getFileServerConfig";
}

bool HttpPost::GetFileServerConfig(std::string& response) {
    HttpStringHandler http_handler;
    HttpRequest http_request(get_file_server_config_url_, &http_handler);
    if (!AppendHeaderS(http_request)) {
        return false;
    }

    http_request.SetData("{}");
    http_request.SetTimeout(10);
    LOG_DEBUG("HTTP POST request_bytes:{}", 2);

    const auto status = http_request.Submit(HttpRequestMethod::kPost);
    if (status != 200) {
        LOG_ERRO("HTTP POST failed, status:{} response_bytes:{}", status, http_handler.GetData().size());
        return false;
    }
    LOG_DEBUG("HTTP POST completed, status:{} response_bytes:{}", status, http_handler.GetData().size());
    response = http_handler.GetData();
    return true;
}

void HttpPost::SetAppInfo(const std::string& app_key, const std::string& app_secret) {
    if (app_key.empty() || app_secret.empty()) {
        app_key_.clear();
        app_secret_.clear();
        LOG_ERRO("{}", "platform application credentials must provide both key and secret");
        return;
    }
    app_key_    = app_key;
    app_secret_ = app_secret;
}

bool HttpPost::HasAppInfo() const noexcept {
    return !app_key_.empty() && !app_secret_.empty();
}

bool HttpPost::LoadAppInfoFromRuntime() {
    std::string app_key;
    std::string app_secret;
    const auto key_state    = LoadCredentialFile(kAppKeyFileEnv, app_key);
    const auto secret_state = LoadCredentialFile(kAppSecretFileEnv, app_secret);
    if (key_state == CredentialLoadState::kLoaded && secret_state == CredentialLoadState::kLoaded) {
        app_key_    = std::move(app_key);
        app_secret_ = std::move(app_secret);
        return true;
    }

    app_key_.clear();
    app_secret_.clear();
    if (key_state == CredentialLoadState::kMissing && secret_state == CredentialLoadState::kMissing) {
        LOG_INFO("{}",
                 "platform application credentials are not configured; signed manager requests are disabled");
    } else {
        LOG_ERRO("platform application credentials are incomplete: key={}, secret={}",
                 CredentialStateName(key_state), CredentialStateName(secret_state));
    }
    return false;
}

bool HttpPost::AppendHeaderS(HttpRequest& hrt) {
    if (!HasAppInfo()) {
        LOG_ERRO("{}",
                 "signed manager request rejected because platform application credentials are unavailable");
        return false;
    }
    hrt.SetContentType("application/json");
    std::string request_id("CWAI_Analyzer_");
    request_id += cosmo::util::GenerateUUID();
    hrt.AppendHeader("RequestId", request_id);
    hrt.AppendHeader("AppKey", app_key_);

    std::string nonce = cosmo::util::GenerateUUID();
    hrt.AppendHeader("Nonce", nonce);

    auto now      = std::chrono::system_clock::now().time_since_epoch();
    auto cur_time = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    char cur_time_buf[16] = {0};
    snprintf(cur_time_buf, sizeof(cur_time_buf), "%013lld", static_cast<long long>(cur_time));
    hrt.AppendHeader("CurTime", cur_time_buf);

    std::string sha_str   = nonce + app_secret_ + cur_time_buf;
    std::string head_sha1 = cosmo::util::Sha1(sha_str);
    hrt.AppendHeader("CheckSum", head_sha1);
    return true;
}

}  // namespace cosmo::network::http
