// Linkage DTO definitions

#pragma once

#include <system_error>

#include "service/infra/dto/InfraMsgTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {

/// Linkage strategy output unit — query result for a single strategy.
struct LinkageStrategyOutputUnit {
    std::string id;
    std::string name;
    std::string workFlow;
    bool status{false};
    friend void to_json(nlohmann::json& j, const LinkageStrategyOutputUnit& v);
    friend void from_json(const nlohmann::json& j, LinkageStrategyOutputUnit& v);
};

namespace Linkage {
    struct MsgStoragesRecv : public MsgRecvHead {};

    struct MsgStoragesSend : public MsgSendHead {
        struct ResData {
            std::vector<StorageList> storages;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgStoragesSend& v);
    void from_json(const nlohmann::json& j, MsgStoragesSend& v);

    struct MsgAddRecv : public MsgRecvHead {
        std::string name;
        std::string workFlow;
    };

    void to_json(nlohmann::json& j, const MsgAddRecv& v);
    void from_json(const nlohmann::json& j, MsgAddRecv& v);

    struct MsgAddSend : public MsgSendHead {
        struct ResData {
            std::string id;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgAddSend& v);
    void from_json(const nlohmann::json& j, MsgAddSend& v);

    struct MsgDeleteRecv : public MsgRecvHead {
        std::string id;
    };

    void to_json(nlohmann::json& j, const MsgDeleteRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteRecv& v);

    struct MsgDeleteSend : public MsgSendHead {};
    struct MsgUpdateRecv : public MsgRecvHead {
        std::string name;
        std::string id;
        std::string workFlow;
    };

    void to_json(nlohmann::json& j, const MsgUpdateRecv& v);
    void from_json(const nlohmann::json& j, MsgUpdateRecv& v);

    struct MsgUpdateSend : public MsgSendHead {};

    struct MsgPageRecv : public MsgRecvHead, public MsgConditionPage {
        std::string name;
    };

    void to_json(nlohmann::json& j, const MsgPageRecv& v);
    void from_json(const nlohmann::json& j, MsgPageRecv& v);

    struct MsgPageSend : public MsgSendHead {
        struct ResData {
            size_t total{0};
            std::vector<LinkageStrategyOutputUnit> tasks;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgPageSend& v);
    void from_json(const nlohmann::json& j, MsgPageSend& v);

    struct MsgSwitchRecv : public MsgRecvHead {
        std::string id;
        bool enable{false};
    };

    void to_json(nlohmann::json& j, const MsgSwitchRecv& v);
    void from_json(const nlohmann::json& j, MsgSwitchRecv& v);

    struct MsgSwitchSend : public MsgSendHead {};
}  // namespace Linkage
}  // namespace cosmo
