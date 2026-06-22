// StatusMsgTypes — Status/query types — MsgTaskStatus, DeviceMemStatus, MsgQueryLogs*.

#include "service/task/dto/StatusMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgQueryTaskStatusRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["tasks"] = v.tasks;
}

void from_json(const nlohmann::json& j, MsgQueryTaskStatusRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, tasks);
}

void to_json(nlohmann::json& j, const MsgQueryTaskStatusSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["status"] = v.status;
}

void from_json(const nlohmann::json& j, MsgQueryTaskStatusSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, status);
}

void to_json(nlohmann::json& j, const MsgQueryTaskInfoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["taskId"] = v.taskId;
    j["info"]   = v.info;
    j["action"] = v.action;
}

void from_json(const nlohmann::json& j, MsgQueryTaskInfoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, taskId);
    JSON_OPT(j, v, info);
    JSON_OPT(j, v, action);
}

void to_json(nlohmann::json& j, const MsgQueryTaskInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["info"]      = v.info;
    j["action"]    = v.action;
    j["streamUrl"] = v.streamUrl;
}

void from_json(const nlohmann::json& j, MsgQueryTaskInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, info);
    JSON_OPT(j, v, action);
    JSON_OPT(j, v, streamUrl);
}

void to_json(nlohmann::json& j, const MsgQueryDeviceMemStatusSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["status"]       = v.status;
    j["totalInUsing"] = v.totalInUsing;
    j["totalMalloc"]  = v.totalMalloc;
}

void from_json(const nlohmann::json& j, MsgQueryDeviceMemStatusSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, status);
    JSON_OPT(j, v, totalInUsing);
    JSON_OPT(j, v, totalMalloc);
}

void to_json(nlohmann::json& j, const MsgQueryLogsRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["pageNum"]  = v.pageNum;
    j["pageSize"] = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgQueryLogsRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

void to_json(nlohmann::json& j, const MsgQueryLogsSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryLogsSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgTaskStatus& v) {
    JSON_OPT(j, v, channelId);
    JSON_OPT(j, v, taskId);
    JSON_OPT(j, v, streamUrl);
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmVersion);
    JSON_OPT(j, v, actionStatus);
    JSON_OPT(j, v, nodeDurationInfos);
}

void to_json(nlohmann::json& j, const MsgTaskStatus& v) {
    j["channelId"]         = v.channelId;
    j["taskId"]            = v.taskId;
    j["streamUrl"]         = v.streamUrl;
    j["algorithmId"]       = v.algorithmId;
    j["algorithmName"]     = v.algorithmName;
    j["algorithmVersion"]  = v.algorithmVersion;
    j["actionStatus"]      = v.actionStatus;
    j["nodeDurationInfos"] = v.nodeDurationInfos;
}

void from_json(const nlohmann::json& j, DeviceMemStatus& v) {
    JSON_OPT(j, v, threadId);
    JSON_OPT(j, v, duration);
    JSON_OPT(j, v, backtrace);
}

void to_json(nlohmann::json& j, const DeviceMemStatus& v) {
    j["threadId"]  = v.threadId;
    j["duration"]  = v.duration;
    j["backtrace"] = v.backtrace;
}

void from_json(const nlohmann::json& j, DeviceMemPoolStatus& v) {
    JSON_OPT(j, v, poolSize);
    JSON_OPT(j, v, freeCnt);
    JSON_OPT(j, v, mallocCnt);
    JSON_OPT(j, v, mallocPoolStatus);
}

void to_json(nlohmann::json& j, const DeviceMemPoolStatus& v) {
    j["poolSize"]         = v.poolSize;
    j["freeCnt"]          = v.freeCnt;
    j["mallocCnt"]        = v.mallocCnt;
    j["mallocPoolStatus"] = v.mallocPoolStatus;
}

void from_json(const nlohmann::json& j, MsgQueryLogsSend::ResData& v) {
    JSON_OPT(j, v, totalCount);
    JSON_OPT(j, v, path);
    JSON_OPT(j, v, logs);
}

void to_json(nlohmann::json& j, const MsgQueryLogsSend::ResData& v) {
    j["totalCount"] = v.totalCount;
    j["path"]       = v.path;
    j["logs"]       = v.logs;
}

}  // namespace cosmo
