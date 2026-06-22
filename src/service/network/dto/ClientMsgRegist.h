// Client-side registration message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/HeartbeatTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

// Registration request
struct CMsgRegistReq : public MsgRecvHead {
    std::string aiHostUrl;
    std::string devId;
    std::string engineType;  // nvidiaT4: T4 (default), ascend: Ascend, nividiaA30/A5000/A2
                             //
    std::string supplier{"CWAI"};
    std::string aiHostVersion;
    int analysisType{0};
    int devType{0};  // 0: AAE algorithm box, 1: third-party, 2: AAE lite
};

void to_json(nlohmann::json& j, const CMsgRegistReq& v);
void from_json(const nlohmann::json& j, CMsgRegistReq& v);

struct CMsgRegistConfig {
    std::string ntpServerUrl;
    std::string h265TransServerUrl;
    std::string tryPullFlowCounts;
    std::string bodyTrackParameter;
    std::string packetDiscardRatio;
    MsgFileServerInfo fileServer;
    friend void to_json(nlohmann::json& j, const CMsgRegistConfig& v);
    friend void from_json(const nlohmann::json& j, CMsgRegistConfig& v);
};
// Registration response
struct CMsgRegistRsp : public MsgSendHead {
    struct ResData {
        std::string devId;
        std::string devName;
        std::string managerUrl;
        int heartBeatPeriod{30};
        std::string appKey;
        std::string appSecret;
        std::vector<std::string>
            initAlgorithmList;  // Pre-init algorithms: 40 = face feature, 43 = body feature
        CMsgRegistConfig config;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const CMsgRegistRsp& v);
void from_json(const nlohmann::json& j, CMsgRegistRsp& v);

}  // namespace cosmo
