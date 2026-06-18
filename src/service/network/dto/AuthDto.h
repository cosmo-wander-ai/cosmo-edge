// Auth DTO definitions (extracted from MessageAuthHandler.h)

#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace Auth {
    struct MsgDoLoginRecv : public MsgRecvHead {
        std::string account;  // Login username
        std::string pwd;      // MD5-hashed password
    };

    void to_json(nlohmann::json& j, const MsgDoLoginRecv& v);
    void from_json(const nlohmann::json& j, MsgDoLoginRecv& v);

    // Login response
    struct MsgDoLoginSend : public MsgSendHead {
        struct ResData {
            std::string accountName;
            std::string mtk;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgDoLoginSend& v);
    void from_json(const nlohmann::json& j, MsgDoLoginSend& v);

    // Modify password request
    struct MsgModifyPasswordRecv : public MsgRecvHead {
        std::string mtk;
        std::string passwdOld;
        std::string passwdNew;
    };

    void to_json(nlohmann::json& j, const MsgModifyPasswordRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyPasswordRecv& v);

    // Modify password response
    struct MsgModifyPasswordSend : public MsgSendHead {};
}  // namespace Auth
}  // namespace cosmo
