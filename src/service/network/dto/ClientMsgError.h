// Client-side error event message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "service/system/dto/SystemMsgTypes.h"
#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

struct OnErrorsReason {
    std::string aiHostId;          // Device ID
    std::string aiHostIp;          // Device IP
    std::string videoChannelId;    // Video channel ID
    std::string videoChannelName;  // Video channel name
    std::string streamUrl;         // Stream URL
    std::string algorithmId;       // Algorithm ID
    std::string algorithmName;     // Algorithm name
    std::string algorithmVersion;  // Algorithm version
    std::string message;           // Error message
    friend void to_json(nlohmann::json& j, const OnErrorsReason& v);
    friend void from_json(const nlohmann::json& j, OnErrorsReason& v);
};
using OnErrorsReasonPtr = std::shared_ptr<OnErrorsReason>;

struct ActionInfoSonMsg {
    std::string channelId;
    std::string taskId;
    std::string actionId;
    float fps{0.0};
    std::string queueName;
    friend void to_json(nlohmann::json& j, const ActionInfoSonMsg& v);
    friend void from_json(const nlohmann::json& j, ActionInfoSonMsg& v);
};
using ActionInfoSonMsgPtr = std::shared_ptr<ActionInfoSonMsg>;
struct ActionInfoMsg {
    std::string actionId;
    std::string channelId;
    bool bChannelReuse{false};
    float maxTaskFps{0.0};
    std::string queueName;
    std::vector<ActionInfoSonMsg> sons;
    friend void to_json(nlohmann::json& j, const ActionInfoMsg& v);
    friend void from_json(const nlohmann::json& j, ActionInfoMsg& v);
};
using ActionInfoMsgPtr = std::shared_ptr<ActionInfoMsg>;

struct OnErrorsDetail {
    int type{10001};
    OnErrorsReason reason;
    friend void to_json(nlohmann::json& j, const OnErrorsDetail& v);
    friend void from_json(const nlohmann::json& j, OnErrorsDetail& v);
};
using OnErrorsDetailPtr = std::shared_ptr<OnErrorsDetail>;

// Task error report event request
struct CMsgOnErrorsReq : public MsgRecvHead {
    std::string devId;      // Device ID
    std::string taskId;     // Task ID
    std::string timestamp;  // UTC timestamp (ms)
    std::vector<OnErrorsDetail> details;
    std::vector<ActionStatus> actionStatus;
    std::vector<ActionInfoMsg> actionInfos;
};

void to_json(nlohmann::json& j, const CMsgOnErrorsReq& v);
void from_json(const nlohmann::json& j, CMsgOnErrorsReq& v);

// Task error report event response
struct CMsgOnErrorsRsp : public MsgSendHead {};

}  // namespace cosmo
