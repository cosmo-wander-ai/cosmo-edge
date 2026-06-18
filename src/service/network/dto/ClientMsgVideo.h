// Client-side video playback message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

// Get video playback info request
struct CMsgGetVideoPlayReq : public MsgRecvHead {
    std::string devId;  // Device ID
    std::string videoChannelId;
};

void to_json(nlohmann::json& j, const CMsgGetVideoPlayReq& v);
void from_json(const nlohmann::json& j, CMsgGetVideoPlayReq& v);

struct CMsgGetVideoPlayRsp : public MsgSendHead {
    struct ResData {
        std::string devId;
        std::string videoChannelId;
        std::string videoChannelName;
        std::string streamUrl;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const CMsgGetVideoPlayRsp& v);
void from_json(const nlohmann::json& j, CMsgGetVideoPlayRsp& v);

}  // namespace cosmo
