// Client-side collection event message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"

namespace cosmo {

struct CMsgLabelObject {
    std::string atomicCode;
    std::string lableName;
    std::string confidence;
    MsgRectReal rectangle;
    friend void to_json(nlohmann::json& j, const CMsgLabelObject& v);
    friend void from_json(const nlohmann::json& j, CMsgLabelObject& v);
};

// Collection report event request
struct CMsgCollectRptReq : public MsgRecvHead {
    std::string messageId;
    std::string devId;                       // Device ID
    std::string taskId;                      // Task ID
    std::string videoChannelId;              // Video channel
    std::string timestamp;                   // UTC timestamp (ms)
    std::string algorithmId;                 // Algorithm ID
    std::string algorithmCode;               // Algorithm code
    std::string algorithmName;               // Algorithm name
    std::string orignalPicture;              // Original picture
    std::vector<CMsgLabelObject> lableList;  // Label list
};

void to_json(nlohmann::json& j, const CMsgCollectRptReq& v);
void from_json(const nlohmann::json& j, CMsgCollectRptReq& v);

// Collection report event response
struct CMsgCollectRptRsp : public MsgSendHead {};

}  // namespace cosmo
