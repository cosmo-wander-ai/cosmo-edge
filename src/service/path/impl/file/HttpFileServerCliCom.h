/// @file HttpFileServerCliCom.h
/// @brief File-server upload/download task message types for the worker
///        thread pool in FileServiceImpl.
#pragma once

#include <string>

#include "network/msg/MsgTask.h"
#include "service/path/impl/file/HttpFileServerCli.h"

namespace cosmo::service {

using HFSCallBacK = std::function<void(std::string, bool, void* ptr)>;

enum class FileServerMsgId {
    kUploadFile = 0,
    kPointUploadFile,
    kGetDownload,
};

// Standard upload task
struct CUploadFileTask : cosmo::MsgTask {
    CUploadFileTask(const std::string& req_id, const HFSCallBacK& cb_func, void* user_data,
                    const std::string& bkt, const std::string& url, const FMsgRspGetFileUrl& req_crt,
                    const std::string& fpath)
        : res_get_file_url(req_crt),
          task_id(req_id),
          callback(cb_func),
          bucket(bkt),
          file_url(url),
          req(user_data),
          upload_filepath(fpath) {}
    CUploadFileTask(std::string&& req_id, HFSCallBacK&& cb_func, void* user_data, std::string&& bkt,
                    std::string&& url, FMsgRspGetFileUrl&& req_crt, std::string&& fpath)
        : res_get_file_url(std::move(req_crt)),
          task_id(std::move(req_id)),
          callback(std::move(cb_func)),
          bucket(std::move(bkt)),
          file_url(std::move(url)),
          req(user_data),
          upload_filepath(std::move(fpath)) {}

    ~CUploadFileTask() override = default;

    FMsgRspGetFileUrl res_get_file_url;
    std::string task_id;
    HFSCallBacK callback;
    std::string bucket;
    std::string file_url;
    void* req;
    std::string upload_filepath;
    FMsgReqGetFileUrl req_get_file_url;
};

// Resumable upload task
struct CPointUploadFileTask : cosmo::MsgTask {
    CPointUploadFileTask(const std::string& req_id, const HFSCallBacK& cb_func, void* user_data,
                         const FMsgRspGetFileUrl& req_crt, const std::string& fpath)
        : res_get_file_url(req_crt),
          task_id(req_id),
          callback(cb_func),
          req(user_data),
          upload_filepath(fpath) {}
    CPointUploadFileTask(std::string&& req_id, HFSCallBacK&& cb_func, void* user_data,
                         FMsgRspGetFileUrl&& req_crt, std::string&& fpath)
        : res_get_file_url(std::move(req_crt)),
          task_id(std::move(req_id)),
          callback(std::move(cb_func)),
          req(user_data),
          upload_filepath(std::move(fpath)) {}

    ~CPointUploadFileTask() override = default;

    FMsgRspGetFileUrl res_get_file_url;
    std::string task_id;
    HFSCallBacK callback;
    void* req;
    std::string upload_filepath;

    FMsgReqGetFileUrl req_get_file_url;
    FMsgReqPUpFile req_point_upload;
};

// Async download task
struct CGetDownloadTask : cosmo::MsgTask {
    CGetDownloadTask(const std::string& req_id, const HFSCallBacK& cb_func, void* user_data,
                     const FMsgRspUpFile& rsp, const std::string& dl_url, const std::string& dl_path)
        : resp_up_file(rsp),
          task_id(req_id),
          callback(cb_func),
          req(user_data),
          download_file_url(dl_url),
          download_filepath(dl_path) {}
    CGetDownloadTask(std::string&& req_id, HFSCallBacK&& cb_func, void* user_data, FMsgRspUpFile&& rsp,
                     std::string&& dl_url, std::string&& dl_path)
        : resp_up_file(std::move(rsp)),
          task_id(std::move(req_id)),
          callback(std::move(cb_func)),
          req(user_data),
          download_file_url(std::move(dl_url)),
          download_filepath(std::move(dl_path)) {}

    ~CGetDownloadTask() override = default;

    FMsgRspUpFile resp_up_file;
    std::string task_id;
    HFSCallBacK callback;
    void* req;
    std::string download_file_url;
    std::string download_filepath;
};

}  // namespace cosmo::service
