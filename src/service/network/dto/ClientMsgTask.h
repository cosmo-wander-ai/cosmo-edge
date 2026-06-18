// Client-side task query message types.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {

// Task query request
struct CMsgQueryTaskListReq : public MsgRecvHead {
    std::string devId;  // Device ID
};

void to_json(nlohmann::json& j, const CMsgQueryTaskListReq& v);
void from_json(const nlohmann::json& j, CMsgQueryTaskListReq& v);

// Task query response
struct CMsgQueryTaskListRsp : public MsgSendHead {
    struct ResData {
        std::vector<MsgTaskCreateRecv> task;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const CMsgQueryTaskListRsp& v);
void from_json(const nlohmann::json& j, CMsgQueryTaskListRsp& v);

struct CMsgOnCompleteReq : public MsgRecvHead {
    std::string taskId;
};

void to_json(nlohmann::json& j, const CMsgOnCompleteReq& v);
void from_json(const nlohmann::json& j, CMsgOnCompleteReq& v);

// Task completion response
struct CMsgOnCompleteRsp : public MsgSendHead {};

}  // namespace cosmo
