#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "network/msg/MsgTask.h"

namespace cosmo::network::http {

using HttpRequestToken                              = std::uint64_t;
constexpr HttpRequestToken kInvalidHttpRequestToken = 0;

// HTTP request type codes (used for internal message routing)
enum class HttpReqType {
    kComGen = 0,
    kSetMaster,
    kInfo,
    kTaskCreate,
    kTaskCancel,
    kQueryTask,
    kConfig,
    kFaceFeature,
    kLoadFaceSet,
    kPedeFeature,
    kLoadPedeSet,
    kSnapAnalysis,
    kDevDiagnosis,
    kDataCollection,
    kCreateVideoPreview,
    kStopVideoPreview,
    kSetAlgorithmFps,
    kWorkClothesFeature,
    kNormalFaceFeature,
    kProbe,
    kSetFaceSetInfo,
    kSetPedeTrackParam,
    kPreLoadAlgorithm,
    kSetLogLevel,
    kEnd,
};

constexpr int kUserMsgIdBase = 5000;

enum class InnerMsgId {
    kHttpReq      = kUserMsgIdBase + 1,
    kHttpAck      = kUserMsgIdBase + 2,
    kHttpOctetReq = kUserMsgIdBase + 3,
    kHttpOctetAck = kUserMsgIdBase + 4,
    kHttpQuitAck  = kUserMsgIdBase + 5,
};

enum class HttpResponseCode {
    kOk                 = 200,
    kBadRequest         = 400,
    kNeedAuthenticate   = 401,
    kNotFound           = 404,
    kBadMethod          = 405,
    kPayloadTooLarge    = 413,
    kInternalError      = 500,
    kServiceUnavailable = 503,
};

// Response code to message mapping
inline const std::map<int, std::string>& GetHttpResCodeMsg() {
    static const std::map<int, std::string> kMap = {
        {static_cast<int>(HttpResponseCode::kOk), "OK"},
        {static_cast<int>(HttpResponseCode::kBadRequest), "BAD REQUEST"},
        {static_cast<int>(HttpResponseCode::kNeedAuthenticate), "NEED AUTHENTICATE"},
        {static_cast<int>(HttpResponseCode::kNotFound), "NOT FOUND"},
        {static_cast<int>(HttpResponseCode::kBadMethod), "METHOD NOT FOR THIS URI"},
        {static_cast<int>(HttpResponseCode::kPayloadTooLarge), "PAYLOAD TOO LARGE"},
        {static_cast<int>(HttpResponseCode::kInternalError), "INTERNAL SERVER ERROR"},
        {static_cast<int>(HttpResponseCode::kServiceUnavailable), "SERVICE UNAVAILABLE"},
    };
    return kMap;
}

// HTTP request task
struct HttpReqTask : cosmo::MsgTask {
    HttpRequestToken request_token{kInvalidHttpRequestToken};
    std::chrono::steady_clock::time_point request_time;
    std::string interface;
    std::string x_forwarded_for;
    std::string mtk;
    std::string principal;
    std::string http_method;
    std::string upload_id;
    std::string upload_offset;
    std::string range_request;
    std::string body;
    bool has_tmp_path{false};
    std::string tmp_file_path;
    std::string multipart_file_path;
    std::string multipart_file_name;
    std::uint64_t multipart_file_size{0};
    // Keeps the server-side multipart spool quota reserved until this task is
    // processed, rejected, or drained during shutdown.
    std::shared_ptr<void> multipart_spool_reservation;

    ~HttpReqTask() override = default;
};

// HTTP response task
struct HttpAckTask : cosmo::MsgTask {
    int http_ack_code;
    HttpRequestToken request_token;
    std::string response;
    std::string request_id;
    std::string file_path;
    std::string file_name;
    std::string range_request;
    bool delete_file_after_send{false};

    explicit HttpAckTask(int ack_code, HttpRequestToken token, std::string response_body,
                         std::string response_request_id)
        : http_ack_code(ack_code),
          request_token(token),
          response(std::move(response_body)),
          request_id(std::move(response_request_id)) {}

    ~HttpAckTask() override;
};

// Callback configuration for path resolution (injected from service layer)
struct HttpServerCallbacks {
    std::function<std::string()> get_upload_tmp_path;
    std::function<std::string()> get_user_data_path;
    std::function<std::string()> get_log_path;
};

}  // namespace cosmo::network::http
