#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/MsgBaseTypes.h"
#include "util/MsgDynamicElement.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/HeartbeatTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

///////////////////////////////////////////////////////////////////////////

struct MsgInterfaceTestRecv : public MsgRecvHead {
    std::string test;  // Task ID (globally unique)
};

void to_json(nlohmann::json& j, const MsgInterfaceTestRecv& v);
void from_json(const nlohmann::json& j, MsgInterfaceTestRecv& v);

// Create task response
struct MsgInterfaceTestSend : public MsgSendHead {};

// Delete task request
struct MsgTaskCancleRecv : public MsgRecvHead {
    std::string mvDebug;
    std::string taskId;  // Task ID (globally unique)
};

void to_json(nlohmann::json& j, const MsgTaskCancleRecv& v);
void from_json(const nlohmann::json& j, MsgTaskCancleRecv& v);

// Delete task response
struct MsgTaskCancleSend : public MsgSendHead {};

struct MsgPTaskCancleRecv : public MsgRecvHead {
    std::string mvDebug;
    std::string taskId;  // Task ID (globally unique)
    std::string algorithmCode;
};

void to_json(nlohmann::json& j, const MsgPTaskCancleRecv& v);
void from_json(const nlohmann::json& j, MsgPTaskCancleRecv& v);

// Delete picture task response
struct MsgPTaskCancleSend : public MsgSendHead {};

// Picture algorithm update trigger
struct MsgOperateNodeRecv : public MsgRecvHead {
    std::string devId;
    std::string operateType;  // Operation type
};

void to_json(nlohmann::json& j, const MsgOperateNodeRecv& v);
void from_json(const nlohmann::json& j, MsgOperateNodeRecv& v);

// Picture algorithm update trigger response
struct MsgOperateNodeSend : public MsgSendHead {};

// Info request
struct MsgInfoRecv : public MsgRecvHead {
    std::string devId;  // Device ID
};

void to_json(nlohmann::json& j, const MsgInfoRecv& v);
void from_json(const nlohmann::json& j, MsgInfoRecv& v);

// Info response
struct MsgInfoSend : public MsgSendHead, public CMsgHeartBeatReq {};

void to_json(nlohmann::json& j, const MsgInfoSend& v);
void from_json(const nlohmann::json& j, MsgInfoSend& v);

// Probe request
struct MsgProbeRecv : public MsgRecvHead {};

// Probe response
struct MsgProbeSend : public MsgSendHead {};

struct MsgGraphicsMemoryRecv : public MsgRecvHead {
    std::string test;  // Task ID (globally unique)
};

void to_json(nlohmann::json& j, const MsgGraphicsMemoryRecv& v);
void from_json(const nlohmann::json& j, MsgGraphicsMemoryRecv& v);
// Graphics memory usage response
struct MsgGraphicsMemorySend : public MsgSendHead {
    std::string debugMessage;
};

void to_json(nlohmann::json& j, const MsgGraphicsMemorySend& v);
void from_json(const nlohmann::json& j, MsgGraphicsMemorySend& v);

struct MsgViewRoutesRecv : public MsgRecvHead {
    int viewCounts;
};

void to_json(nlohmann::json& j, const MsgViewRoutesRecv& v);
void from_json(const nlohmann::json& j, MsgViewRoutesRecv& v);

struct MsgViewRoutesSend : public MsgSendHead {};
}  // namespace cosmo
