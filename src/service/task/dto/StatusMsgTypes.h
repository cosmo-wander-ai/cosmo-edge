// Status/query types — MsgTaskStatus, DeviceMemStatus, MsgQueryLogs*.
// Modular DTO header.

#pragma once

#include "service/system/dto/SystemMsgTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/AlgorithmMsgTypes.h"
#include "util/dto/HeartbeatTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {

struct MsgTaskStatus {
    std::string channelId;
    std::string taskId;
    std::string streamUrl;         // Stream URL
    std::string algorithmId;       // Algorithm ID
    std::string algorithmName;     // Algorithm name
    std::string algorithmVersion;  // Algorithm version
    std::vector<ActionStatus> actionStatus;
    std::vector<AlgActionNodeDurationInfo> nodeDurationInfos;
    friend void to_json(nlohmann::json& j, const MsgTaskStatus& v);
    friend void from_json(const nlohmann::json& j, MsgTaskStatus& v);
};

struct MsgQueryTaskStatusRecv : public MsgRecvHead {
    std::vector<std::string> tasks;
};

void to_json(nlohmann::json& j, const MsgQueryTaskStatusRecv& v);
void from_json(const nlohmann::json& j, MsgQueryTaskStatusRecv& v);
struct MsgQueryTaskStatusSend : public MsgSendHead {
    std::vector<MsgTaskStatus> status;
};

void to_json(nlohmann::json& j, const MsgQueryTaskStatusSend& v);
void from_json(const nlohmann::json& j, MsgQueryTaskStatusSend& v);

struct MsgQueryTaskInfoRecv : public MsgRecvHead {
    std::string taskId;
    bool info{false};
    bool action{false};
};

void to_json(nlohmann::json& j, const MsgQueryTaskInfoRecv& v);
void from_json(const nlohmann::json& j, MsgQueryTaskInfoRecv& v);
struct MsgQueryTaskInfoSend : public MsgSendHead {
    std::string streamUrl;
    MsgTaskCreateRecv info;
    ActionAlg action;
};

void to_json(nlohmann::json& j, const MsgQueryTaskInfoSend& v);
void from_json(const nlohmann::json& j, MsgQueryTaskInfoSend& v);

struct DeviceMemStatus {
    pid_t threadId{0};
    int64_t duration{0};
    std::string backtrace;
    friend void to_json(nlohmann::json& j, const DeviceMemStatus& v);
    friend void from_json(const nlohmann::json& j, DeviceMemStatus& v);
};

struct DeviceMemPoolStatus {
    size_t poolSize{0};
    size_t freeCnt{0};
    size_t mallocCnt{0};
    std::vector<DeviceMemStatus> mallocPoolStatus;
    friend void to_json(nlohmann::json& j, const DeviceMemPoolStatus& v);
    friend void from_json(const nlohmann::json& j, DeviceMemPoolStatus& v);
};

struct MsgQueryDeviceMemStatusRecv : public MsgRecvHead {
    bool backtrace{false};
};
struct MsgQueryDeviceMemStatusSend : public MsgSendHead {
    size_t totalMalloc{0};
    size_t totalInUsing{0};
    std::vector<DeviceMemPoolStatus> status;
};

void to_json(nlohmann::json& j, const MsgQueryDeviceMemStatusSend& v);
void from_json(const nlohmann::json& j, MsgQueryDeviceMemStatusSend& v);

// Log list query
struct MsgQueryLogsRecv : public MsgRecvHead {
    int pageNum{1};
    int pageSize{10};
};

void to_json(nlohmann::json& j, const MsgQueryLogsRecv& v);
void from_json(const nlohmann::json& j, MsgQueryLogsRecv& v);
struct MsgQueryLogsSend : public MsgSendHead {
    struct ResData {
        int totalCount{0};
        std::string path;
        std::vector<std::string> logs;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryLogsSend& v);
void from_json(const nlohmann::json& j, MsgQueryLogsSend& v);

}  // namespace cosmo
