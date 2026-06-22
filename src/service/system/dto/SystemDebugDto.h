#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo::System {

struct MsgModifyDebugModeRecv : public MsgRecvHead {
    bool debugModeOpen{false};
};

void to_json(nlohmann::json& j, const MsgModifyDebugModeRecv& v);
void from_json(const nlohmann::json& j, MsgModifyDebugModeRecv& v);

struct MsgModifyDebugModeSend : public MsgSendHead {};

struct MsgQueryDebugModeRecv : public MsgRecvHead {};

struct MsgQueryDebugModeSend : public MsgSendHead {
    struct ResData {
        bool debugModeOpen{false};
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryDebugModeSend& v);
void from_json(const nlohmann::json& j, MsgQueryDebugModeSend& v);

struct MsgModifyShiledActionsRecv : public MsgRecvHead {
    std::vector<std::string> shiledActions;
};

void to_json(nlohmann::json& j, const MsgModifyShiledActionsRecv& v);
void from_json(const nlohmann::json& j, MsgModifyShiledActionsRecv& v);

struct MsgModifyShiledActionsSend : public MsgSendHead {};

struct MsgQueryShiledActionsRecv : public MsgRecvHead {};

struct MsgQueryShiledActionsSend : public MsgSendHead {
    struct ResData {
        std::vector<std::string> shiledActions;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryShiledActionsSend& v);
void from_json(const nlohmann::json& j, MsgQueryShiledActionsSend& v);

// Thread debug info
struct MsgThreadDebugInfoRecv : public MsgRecvHead {};

// Thread debug info response
struct MsgThreadDebugInfoSend : public MsgSendHead {};

struct MsgDebugQuitRecv : public MsgRecvHead {};

struct MsgDebugQuitSend : public MsgSendHead {};

struct MsgDebugSystemMemRecv : public MsgRecvHead {};

struct MsgDebugSystemMemSend : public MsgSendHead {};

struct MsgDictRecv : public MsgRecvHead {
    std::vector<std::string> keys;
};

void to_json(nlohmann::json& j, const MsgDictRecv& v);
void from_json(const nlohmann::json& j, MsgDictRecv& v);

struct MsgDictUnit : public MsgRecvHead {
    std::string key;
    std::vector<MsgDynamicKeyValue> infos;
    friend void to_json(nlohmann::json& j, const MsgDictUnit& v);
    friend void from_json(const nlohmann::json& j, MsgDictUnit& v);
};

struct MsgDictSend : public MsgSendHead {
    struct ResData {
        std::vector<MsgDictUnit> infos;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgDictSend& v);
void from_json(const nlohmann::json& j, MsgDictSend& v);

}  // namespace cosmo::System
