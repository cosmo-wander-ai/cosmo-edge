#pragma once

#include <string>
#include <vector>

#include "network/http/HttpRequest.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"

namespace cosmo::network::http {

enum class CliReqType {
    kCliRegister = 0,
    kCliHeartBeat,
    kCliGetFunc,
    kCliGetTaskLst,
    kCliGetLicense,
    kCliGetVideoUrl,
    kCliReportEvent,
    kCliReportError,
    kCliReportInfo,
    kCliGetFileSrv,
    kCliOnComplete,
    kCliRecordDevDiagnosis,
    kCliReportPollingTime,
    kCliDataCollection,
    kCliReportFaceinfo,
    kCliGetComConfig,
    kCliVideoUploadResult,
    kCliGetMultipleTargetSettingUrl,
    kCliQueryCommodityset
};

class HttpPost {
public:
    HttpPost();
    explicit HttpPost(const std::string& url);

    HttpPost(const HttpPost&)            = delete;
    HttpPost& operator=(const HttpPost&) = delete;

    void SetPostUrl(const std::string& url);
    void SetRegUrl(const std::string& strUrl);
    void SetIpPort(const std::string& ipPort);
    bool SetFile(const std::string& key, const std::string& filename,
                 const std::string& filetype = "text/plain");
    void AppendData(const std::string& key, const std::string& value);

    // Submit request, returns HTTP status code
    long Submit();

    std::string GetContentType() const;
    std::string GetContent() const;

    std::string& GetPostUrl(const CliReqType& type);

    void SetAppInfo(const std::string& app_key, const std::string& app_secret);
    // Set proxy
    void SetProxy(const std::string& proxy, const std::string& username = "",
                  const std::string& password = "");
    // Get underlying HTTP request object
    HttpRequest& GetHttpRequest();

public:
    // Client push interface
    void AppendHeaderS(HttpRequest& hrt);

    template <typename IN, typename OUT>
    bool HttpClientSubmit(const CliReqType& type, IN& rgt_in, OUT& rgt_out) {
        HttpStringHandler http_hnd{};
        HttpRequest http_req(GetPostUrl(type), &http_hnd);

        AppendHeaderS(http_req);
        std::string json_result{};
        if (!cosmo::util::EncodeJson(rgt_in, json_result)) {
            LOG_ERRO("{}", "StructToJson failed");
        }

        if (type != CliReqType::kCliGetFileSrv) {
            http_req.SetData(json_result);
        } else {
            http_req.SetData("{}");
        }
        LOG_INFO("HTTP Post Url[{}], Msg:{}", GetPostUrl(type).c_str(), json_result);

        http_req.SetTimeout(10);

        auto ret = http_req.Submit(HttpRequestMethod::kPost);  // Will rename this next
        if (200 != ret) {
            LOG_ERRO("http poster submit failed, errret[{}]", ret);
            return false;
        }
        LOG_INFO("Msg:{} Get Response is:{}", GetPostUrl(type), http_hnd.GetData());
        if (!cosmo::util::DecodeJson(http_hnd.GetData(), rgt_out)) {
            LOG_ERRO("cosmo::util::DecodeJson failed, [{}]", http_hnd.GetData());
        }
        return true;
    }

private:
    void BoundaryGen();

private:
    HttpStringHandler str_hnd_;
    HttpRequest http_post_;
    std::string data_;
    std::string boundary_;

    std::string app_key_    = "6623141131";
    std::string app_secret_ = "b67e258d8a6c422c7cbf16c1e0ebbb1c9a117a77";

    std::string ip_port_;
    std::string register_url_;
    std::string heart_beat_url_;
    std::string query_algorithm_url_;
    std::string query_task_list_url_;
    std::string get_licence_url_;
    std::string get_video_play_url_;
    std::string on_errors_url_;
    std::string on_info_url_;
    std::string on_events_url_;
    std::string multiple_target_setting_url_;
    std::string get_file_server_config_url_;
    std::string on_complete_;
    std::string record_dev_diagnosis_;
    std::string report_polling_time_;
    std::string data_collection_;
    std::string report_face_info_;
    std::string get_com_config_url_;
    std::string video_upload_result_;
    std::string query_commodity_set_;
};

}  // namespace cosmo::network::http
