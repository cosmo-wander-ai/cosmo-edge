// Client-side info event message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"

namespace cosmo {

struct CMsgOnInfoDetail {
    std::string taskId;
    std::string message;
    std::string videoChannelId;
    std::string resolution;
    std::string videoEncoding;
    std::string fps;
    friend void to_json(nlohmann::json& j, const CMsgOnInfoDetail& v);
    friend void from_json(const nlohmann::json& j, CMsgOnInfoDetail& v);
};

struct CMsgOnInfoReq : public MsgRecvHead {
    int type{10001};
    std::string devId;
    std::string timestamp;
    std::vector<CMsgOnInfoDetail> details;
};

void to_json(nlohmann::json& j, const CMsgOnInfoReq& v);
void from_json(const nlohmann::json& j, CMsgOnInfoReq& v);

// Info event response
struct CMsgonInfoRsp : public MsgSendHead {};

}  // namespace cosmo
