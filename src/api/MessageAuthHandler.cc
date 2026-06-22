// MessageAuthHandler — Message Auth Handler implementation.

#include "api/MessageAuthHandler.h"

#include "service/network/IAuthService.h"
#include "util/Log.h"

namespace cosmo {

static constexpr const char* kTag = "MessageAuthHandler";

MessageAuthHandler::MessageAuthHandler(service::IAuthService& auth_svc) : auth_svc_(auth_svc) {}

// Login
Auth::MsgDoLoginSend MessageAuthHandler::Handle(Auth::MsgDoLoginRecv&& data,
                                                std::error_condition& errc) const {
    Auth::MsgDoLoginSend retData{};

    LOG_INFO("Login attempt for account:{}", data.account);
    auto ret                    = auth_svc_.Login(data.account, data.pwd);
    errc                        = ret.second;
    retData.resData.accountName = data.account;
    retData.resData.mtk         = ret.first;
    return retData;
}

Auth::MsgModifyPasswordSend MessageAuthHandler::Handle(Auth::MsgModifyPasswordRecv&& data,
                                                       std::error_condition& errc) const {
    Auth::MsgModifyPasswordSend retData{};

    LOG_INFO("{}", "Password change request");
    errc = auth_svc_.ChangePasswd(data.mtk, data.passwdOld, data.passwdNew);
    return retData;
}
}  // namespace cosmo