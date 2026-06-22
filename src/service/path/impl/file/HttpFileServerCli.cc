// HttpFileServerCli — HTTP client for file-server upload / download operations.

#include "service/path/impl/file/HttpFileServerCli.h"

#include <chrono>
#include <ctime>

#include "network/http/HttpCommon.h"
#include "service/path/impl/file/FilePost.h"
#include "service/path/impl/file/FileServerReq.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

namespace cosmo::network::http {

static constexpr const char* kTag = "HttpFileServerCli";

/// Minimum length a response string must have to contain valid JSON.
static constexpr size_t kMinJsonLength = 10;

HttpFileServerCli::HttpFileServerCli() {
    BoundaryGen();
}

void HttpFileServerCli::SetIpPort(const std::string& ip_port) {
    if (ip_port.empty()) {
        return;
    }
    std::unique_lock<std::shared_mutex> lck(mtx_);
    if (ip_port != ip_port_) {
        LOG_INFO("{} FileServer Change From {} To {}", kTag, ip_port_, ip_port);
        ip_port_                  = ip_port;
        std::string base_url      = ip_port + "/file/auth/";
        get_upload_file_path_url_ = base_url + "getUploadFilePath";
        upload_file_url_          = base_url + "uploadFile";
        point_upload_file_url_    = base_url + "pointUploadFile";
    }
}

std::string HttpFileServerCli::GetPostUrl(FileServerClientType type) {
    std::shared_lock<std::shared_mutex> lck(mtx_);
    switch (type) {
        case FileServerClientType::kGetUploadFilePath:
            return get_upload_file_path_url_;
        case FileServerClientType::kUploadFile:
            return upload_file_url_;
        case FileServerClientType::kPointUploadFile:
            return point_upload_file_url_;
        default:
            break;
    }
    return get_upload_file_path_url_;
}

void HttpFileServerCli::SetUserToken(const std::string& user, const std::string& token) {
    if (user.empty() || token.empty()) {
        return;
    }
    std::unique_lock<std::shared_mutex> lck(mtx_);
    if ((user != app_info_.user) || (token != app_info_.token)) {
        LOG_INFO("{} FileServer User/Token changed", kTag);
        app_info_.user  = user;
        app_info_.token = token;
    }
}

std::string HttpFileServerCli::GetUser() {
    std::shared_lock<std::shared_mutex> lck(mtx_);
    return app_info_.user;
}

std::string HttpFileServerCli::GetIpPort() {
    std::shared_lock<std::shared_mutex> lck(mtx_);
    return ip_port_;
}

std::string HttpFileServerCli::GetToken() {
    std::shared_lock<std::shared_mutex> lck(mtx_);
    return app_info_.token;
}

void HttpFileServerCli::BoundaryGen() {
    boundary_.reserve(41);
    boundary_.assign(27, '-');
    char buf[15]{};
    snprintf(buf, 15, "0000%10s", std::to_string(std::time(nullptr)).c_str());
    boundary_.append(buf);
}

std::string HttpFileServerCli::FileServerRespTrim(const std::string& strInput) {
    if (strInput.size() < kMinJsonLength) {
        return "";
    }
    size_t pos1 = strInput.find_first_of('{');
    size_t pos2 = strInput.find_last_of('}');
    if (pos1 == std::string::npos || pos2 == std::string::npos) {
        return "";
    }
    return strInput.substr(pos1, pos2 - pos1 + 1);
}

bool HttpFileServerCli::HttpclientSubmit(FileServerClientType type, cosmo::FMsgRspGetFileUrl& rgtIn,
                                         cosmo::FMsgReqGetFileUrl& rgtOut) {
    FileServerReq http_req(GetPostUrl(type));
    http_req.GetHttpRequest().SetContentType("application/json");
    std::string json_result;
    if (!cosmo::util::EncodeJson(rgtIn, json_result)) {
        LOG_ERRO("{} Get_Upload_Url StructToJson failed", kTag);
    }
    http_req.SetMsgBody(json_result);
    int ret_code = static_cast<int>(http_req.Submit());
    if (ret_code != 200) {
        LOG_ERRO("{} Get_Upload_Url HTTP failed, curl return [{}]", kTag, ret_code);
        return false;
    }
    std::string json_str = FileServerRespTrim(http_req.GetContent());
    if (!cosmo::util::DecodeJson(json_str, rgtOut)) {
        LOG_ERRO("{} DecodeJson failed: {}", kTag, json_str);
    }
    return true;
}

bool HttpFileServerCli::HttpclientSubmit(FileServerClientType type, const std::string& rgtIn,
                                         cosmo::FMsgReqGetFileUrl& rgtOut, const std::string& bucket,
                                         const std::string& fileUrl) {
    FilePost http_req(GetPostUrl(type));
    auto& http_post_req = http_req.GetHttpRequest();
    http_post_req.SetContentType("multipart/form-data; boundary=" + http_req.GetBoundary());
    http_post_req.AppendHeader("bucket", bucket);

    // P0-5: read credentials under a single lock scope including is_https
    std::string user;
    std::string token;
    bool is_https = true;
    {
        std::shared_lock<std::shared_mutex> lck(mtx_);
        user     = app_info_.user;
        token    = app_info_.token;
        is_https = app_info_.is_https;
    }
    http_post_req.AppendHeader("user", user);
    http_post_req.AppendHeader("token", token);
    http_post_req.AppendHeader("isHttps", is_https ? "true" : "false");

    LOG_INFO("{} HttpclientSubmit fileUrl[{}]", kTag, fileUrl);
    http_post_req.AppendHeader("fileUrl", fileUrl);
    http_post_req.SetTimeout(200);
    if (!http_req.SetFile("file", rgtIn, "application/octet-stream")) {
        LOG_ERRO("{} Upload_File set file failed", kTag);
        rgtOut.resCode = -1;
        return false;
    }
    auto ret_code = static_cast<int>(http_req.Submit());
    if (ret_code != 200) {
        LOG_ERRO("{} Upload_File HTTP failed, curl return [{}]", kTag, ret_code);
        rgtOut.resCode = -1;
        cosmo::MsgResBase res_msg;
        res_msg.msgCode = std::to_string(ret_code);
        rgtOut.resMsg.push_back(res_msg);
        return false;
    }
    std::string json_str = FileServerRespTrim(http_req.GetContent());
    if (!cosmo::util::DecodeJson(json_str, rgtOut)) {
        LOG_ERRO("{} DecodeJson failed: {}", kTag, json_str);
    }
    return true;
}

// Point upload — not yet implemented, returns true as a stub.
bool HttpFileServerCli::HttpclientSubmit(FileServerClientType /*type*/, const std::string& /*filepath*/,
                                         cosmo::FMsgReqPUpFile& /*rgtOut*/) {
    return true;
}

bool HttpFileServerCli::HttpclientSubmit(FileServerClientType /*type*/, cosmo::FMsgRspUpFile& /*rgtIn*/,
                                         const std::string& downloadfileurl, const std::string& rgtOut) {
    HttpFileHandler http_hnd(rgtOut);
    HttpRequest http_req(downloadfileurl, &http_hnd);
    http_req.SetTimeout(200);
    auto ret_code = static_cast<int>(http_req.Submit(HttpRequestMethod::kGet));
    if (ret_code != 200) {
        LOG_ERRO("{} Get_Download_File HTTP Fail! curl return [{}]", kTag, ret_code);
        return false;
    }
    return true;
}

}  // namespace cosmo::network::http