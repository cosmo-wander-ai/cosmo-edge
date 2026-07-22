// HttpPost — Http Post implementation.

#include "network/http/HttpPost.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <utility>

#include "network/http/HttpCommon.h"
#include "util/CipherUtil.h"
#include "util/JsonStructUtil.h"
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

HttpPost::HttpPost() : http_post_(std::string(""), str_hnd_) {
    BoundaryGen();
    LoadAppInfoFromRuntime();
}

HttpPost::HttpPost(const std::string& url) : http_post_(url, str_hnd_) {
    BoundaryGen();
    LoadAppInfoFromRuntime();
}

void HttpPost::SetPostUrl(const std::string& url) {
    http_post_.SetPostUrl(url);
}

void HttpPost::SetRegUrl(const std::string& strUrl) {
    register_url_ = strUrl;
}

void HttpPost::SetIpPort(const std::string& ipPort) {
    ip_port_                     = ipPort;
    std::string baseUrl          = "http://" + ip_port_ + "/adp-gtw/cwai/api/v1/manager/ai/";
    register_url_                = baseUrl + "register";
    heart_beat_url_              = baseUrl + "heartBeat";
    query_algorithm_url_         = baseUrl + "queryAlgorithm";
    query_task_list_url_         = baseUrl + "queryTaskList";
    get_licence_url_             = baseUrl + "getLicence";
    get_video_play_url_          = baseUrl + "getVideoPlay";
    on_errors_url_               = baseUrl + "onErrors";
    on_info_url_                 = baseUrl + "onInfo";
    on_events_url_               = baseUrl + "onevents";
    multiple_target_setting_url_ = baseUrl + "algorithmProcessConfig";
    get_file_server_config_url_  = baseUrl + "getFileServerConfig";
    on_complete_                 = baseUrl + "onComplete";
    record_dev_diagnosis_ =
        "http://" + ip_port_ + "/adp-gtw/cwai/api/v1/manager/receive/device/diagnose/record";
    report_polling_time_ = "http://" + ip_port_ + "/adp-gtw/cwai/api/v1/manager/polling/saveRecord";
    data_collection_     = baseUrl + "queryAlgorithmThresholds";
    report_face_info_    = baseUrl + "event/face";
    get_com_config_url_  = baseUrl + "comConfig";
    video_upload_result_ = baseUrl + "videoUploadResult";
    query_commodity_set_ = baseUrl + "queryCommoditySet";
}

bool HttpPost::SetFile(const std::string& key, const std::string& filename, const std::string& filetype) {
    data_.append("--" + boundary_ + "\r\n");
    data_.append("Content-Disposition: form-data; name=\"" + key + "\"; filename=\"" + filename + "\"\r\n");
    data_.append("Content-Type: " + filetype + "\r\n\r\n");

    std::ifstream ifs(filename);
    if (ifs.is_open()) {
        char buf[1024]{};
        do {
            ifs.read(buf, sizeof(buf));
            data_.append(buf, ifs.gcount());
        } while (ifs);
    } else {
        return false;
    }
    data_.append("\r\n");
    return true;
}

void HttpPost::AppendData(const std::string& key, const std::string& value) {
    data_.append("--" + boundary_ + "\r\n");
    data_.append("Content-Disposition: form-data; name=\"" + key + "\"\r\n\r\n");
    data_.append(value + "\r\n");
}

long HttpPost::Submit() {
    http_post_.SetData(data_);
    return http_post_.Submit();
}

std::string HttpPost::GetContentType() const {
    return http_post_.GetContentType();
}

std::string HttpPost::GetContent() const {
    return str_hnd_.GetData();
}

std::string& HttpPost::GetPostUrl(const CliReqType& type) {
    switch (type) {
        case CliReqType::kCliRegister:
            return register_url_;
        case CliReqType::kCliHeartBeat:
            return heart_beat_url_;
        case CliReqType::kCliGetFunc:
            return query_algorithm_url_;
        case CliReqType::kCliGetTaskLst:
            return query_task_list_url_;
        case CliReqType::kCliGetLicense:
            return get_licence_url_;
        case CliReqType::kCliGetVideoUrl:
            return get_video_play_url_;
        case CliReqType::kCliReportEvent:
            return on_events_url_;
        case CliReqType::kCliReportError:
            return on_errors_url_;
        case CliReqType::kCliReportInfo:
            return on_info_url_;
        case CliReqType::kCliGetFileSrv:
            return get_file_server_config_url_;
        case CliReqType::kCliOnComplete:
            return on_complete_;
        case CliReqType::kCliRecordDevDiagnosis:
            return record_dev_diagnosis_;
        case CliReqType::kCliReportPollingTime:
            return report_polling_time_;
        case CliReqType::kCliDataCollection:
            return data_collection_;
        case CliReqType::kCliReportFaceinfo:
            return report_face_info_;
        case CliReqType::kCliGetComConfig:
            return get_com_config_url_;
        case CliReqType::kCliVideoUploadResult:
            return video_upload_result_;
        case CliReqType::kCliGetMultipleTargetSettingUrl:
            return multiple_target_setting_url_;
        case CliReqType::kCliQueryCommodityset:
            return query_commodity_set_;
        default:
            break;
    }

    return register_url_;
}

void HttpPost::SetAppInfo(const std::string& app_key, const std::string& app_secret) {
    if (app_key.empty() || app_secret.empty()) {
        app_key_.clear();
        app_secret_.clear();
        LOG_ERRO("platform application credentials must provide both key and secret");
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
        LOG_INFO("platform application credentials are not configured; signed manager requests are disabled");
    } else {
        LOG_ERRO("platform application credentials are incomplete: key={}, secret={}",
                 CredentialStateName(key_state), CredentialStateName(secret_state));
    }
    return false;
}

void HttpPost::BoundaryGen() {
    boundary_.reserve(41);
    boundary_.assign(27, '-');
    char buf[15]{};
    snprintf(buf, 15, "0000%10s", std::to_string(std::time(nullptr)).c_str());
    boundary_.append(buf);
}

void HttpPost::SetProxy(const std::string& proxy, const std::string& username, const std::string& password) {
    http_post_.SetProxy(proxy, username, password);
}

HttpRequest& HttpPost::GetHttpRequest() {
    return http_post_;
}

bool HttpPost::AppendHeaderS(HttpRequest& hrt) {
    if (!HasAppInfo()) {
        LOG_ERRO("signed manager request rejected because platform application credentials are unavailable");
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
