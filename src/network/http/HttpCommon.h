#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "network/msg/MsgTask.h"

struct evhttp_request;

namespace cosmo::network::http {

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
    kOk               = 200,
    kBadRequest       = 400,
    kNeedAuthenticate = 401,
    kNotFound         = 404,
    kBadMethod        = 405,
};

// Response code to message mapping
inline const std::map<int, std::string>& GetHttpResCodeMsg() {
    static const std::map<int, std::string> kMap = {
        {static_cast<int>(HttpResponseCode::kOk), "OK"},
        {static_cast<int>(HttpResponseCode::kBadRequest), "BAD REQUEST"},
        {static_cast<int>(HttpResponseCode::kNeedAuthenticate), "NEED AUTHENTICATE"},
        {static_cast<int>(HttpResponseCode::kNotFound), "NOT FOUND"},
        {static_cast<int>(HttpResponseCode::kBadMethod), "METHOD NOT FOR THIS URI"},
    };
    return kMap;
}

// HTTP request task
struct HttpReqTask : cosmo::MsgTask {
    struct evhttp_request* request;
    std::chrono::steady_clock::time_point request_time;
    std::string interface;
    bool has_tmp_path{false};
    std::string tmp_file_path;

    virtual ~HttpReqTask() = default;
    explicit HttpReqTask(struct evhttp_request* req, std::chrono::steady_clock::time_point time,
                         std::string iface)
        : request(req), request_time(time), interface(std::move(iface)) {}
};

// HTTP response task
struct HttpAckTask : cosmo::MsgTask {
    int http_ack_code;
    struct evhttp_request* request;
    std::string response;
    std::string request_id;
    std::string file_path;
    std::string file_name;

    explicit HttpAckTask(int ackCode, struct evhttp_request* req, std::string&& resp, std::string&& reqID)
        : http_ack_code(ackCode), request(req), response(std::move(resp)), request_id(std::move(reqID)) {}
};

// Callback configuration for path resolution (injected from service layer)
struct HttpServerCallbacks {
    std::function<std::string()> get_upload_tmp_path;
    std::function<std::string()> get_user_data_path;
};

}  // namespace cosmo::network::http
