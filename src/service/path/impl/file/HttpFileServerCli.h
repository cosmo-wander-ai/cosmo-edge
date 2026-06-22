/// @file HttpFileServerCli.h
/// @brief HTTP client for file-server upload / download operations.
#pragma once

#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "network/http/HttpRequest.h"
#include "service/path/dto/FileServerMsgTypes.h"

namespace cosmo::network::http {

/// File-server operation type for URL routing.
enum class FileServerClientType {
    kGetUploadFilePath = 0,
    kUploadFile,
    kPointUploadFile,
    kGetDownload,
};

/// Credential and endpoint information for the file server.
struct FileServerAppInfo {
    std::string bucket;
    std::string user;
    std::string token;
    std::string file_url;
    std::string uuid;
    std::string content_range;
    bool is_https = true;
};

class HttpFileServerCli {
public:
    HttpFileServerCli();

    HttpFileServerCli(const HttpFileServerCli&)            = delete;
    HttpFileServerCli& operator=(const HttpFileServerCli&) = delete;

    void SetIpPort(const std::string& ipPort);

    std::string GetPostUrl(FileServerClientType type);

    void SetUserToken(const std::string& user, const std::string& token);
    std::string GetUser();
    std::string GetToken();

    std::string GetIpPort();

    // Client submit interfaces
    bool HttpclientSubmit(FileServerClientType type, cosmo::FMsgRspGetFileUrl& rgtIn,
                          cosmo::FMsgReqGetFileUrl& rgtOut);
    bool HttpclientSubmit(FileServerClientType type, const std::string& rgtIn,
                          cosmo::FMsgReqGetFileUrl& rgtOut, const std::string& bucket,
                          const std::string& fileUrl);
    // TODO(refactor): point upload is unimplemented — stub always returns true
    bool HttpclientSubmit(FileServerClientType type, const std::string& filepath,
                          cosmo::FMsgReqPUpFile& rgtOut);
    bool HttpclientSubmit(FileServerClientType type, cosmo::FMsgRspUpFile& rgtIn,
                          const std::string& downloadfileurl, const std::string& rgtOut);

private:
    void BoundaryGen();
    std::string FileServerRespTrim(const std::string& strInput);

    std::string boundary_;
    FileServerAppInfo app_info_;
    std::string get_upload_file_path_url_;
    std::string upload_file_url_;
    std::string point_upload_file_url_;
    std::string ip_port_;
    std::shared_mutex mtx_;
};

}  // namespace cosmo::network::http
