// Client-side commodity/things lib message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

struct CMsgFeatureSetElement {
    bool bLocalFile{false};
    std::string token;  // Unique identifier for the item
    std::string url;    // Item image relative URL
    std::string uri;    // Fully assembled URL
    int status;         // Status: 0 = enabled, 1 = disabled (server delete state: 0 = not deleted)
    friend void to_json(nlohmann::json& j, const CMsgFeatureSetElement& v);
    friend void from_json(const nlohmann::json& j, CMsgFeatureSetElement& v);
};

struct CMsgFeatureSetInfo {
    std::string setToken;   // Group ID
    int64_t version{0};     // Latest version (13-digit timestamp)
    float threshold{-1.0};  // Matching threshold
    std::string name;       // Group name
    std::vector<CMsgFeatureSetElement> list;
};

void to_json(nlohmann::json& j, const CMsgFeatureSetInfo& v);
void from_json(const nlohmann::json& j, CMsgFeatureSetInfo& v);

// Things lib query request  /adp-gtw/cwai/api/v1/manager/ai/queryCommoditySet
struct CMsgQueryCommoditySetReq : public MsgRecvHead {
    std::string setToken;       // Things group ID
    int64_t currentVersion{0};  // Current version on device (empty = full sync, non-empty = incremental)
};

void to_json(nlohmann::json& j, const CMsgQueryCommoditySetReq& v);
void from_json(const nlohmann::json& j, CMsgQueryCommoditySetReq& v);

// Things lib query response
struct CMsgQueryCommoditySetRsp : public MsgSendHead {
    CMsgFeatureSetInfo resData;
};

void to_json(nlohmann::json& j, const CMsgQueryCommoditySetRsp& v);
void from_json(const nlohmann::json& j, CMsgQueryCommoditySetRsp& v);

}  // namespace cosmo
