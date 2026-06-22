// HttpPost — Http Post implementation.

#include "network/http/HttpPost.h"

#include <chrono>
#include <ctime>
#include <fstream>

#include "network/http/HttpCommon.h"
#include "util/CipherUtil.h"
#include "util/JsonStructUtil.h"
#include "util/UuidUtil.h"

namespace cosmo::network::http {

HttpPost::HttpPost() : http_post_(std::string(""), str_hnd_) {
    BoundaryGen();
}

HttpPost::HttpPost(const std::string& url) : http_post_(url, str_hnd_) {
    BoundaryGen();
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
    app_key_    = app_key;
    app_secret_ = app_secret;
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

void HttpPost::AppendHeaderS(HttpRequest& hrt) {
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
    snprintf(cur_time_buf, sizeof(cur_time_buf), "%013ld", cur_time);
    hrt.AppendHeader("CurTime", cur_time_buf);

    std::string sha_str   = nonce + app_secret_ + cur_time_buf;
    std::string head_sha1 = cosmo::util::Sha1(sha_str);
    hrt.AppendHeader("CheckSum", head_sha1);
}

}  // namespace cosmo::network::http
