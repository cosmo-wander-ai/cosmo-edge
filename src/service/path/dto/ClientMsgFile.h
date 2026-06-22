// Client-side message types

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

// 6.1.1 Device registration /cwai/api/v1/manager/register
struct MsgFileServer {
    std::string fileServerUrl;
    std::string user;
    std::string token;
    friend void to_json(nlohmann::json& j, const MsgFileServer& v);
    friend void from_json(const nlohmann::json& j, MsgFileServer& v);
};

// 6.1.9 Get file server config /cwai/api/v1/manager/ai/getFileServerConfig
struct CMsgReqGetFileServerConfig : public MsgSendHead {
    MsgFileServer resData;
};

void to_json(nlohmann::json& j, const CMsgReqGetFileServerConfig& v);
void from_json(const nlohmann::json& j, CMsgReqGetFileServerConfig& v);

}  // namespace cosmo
