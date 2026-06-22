// ApiRouterInternal.h — Shared implementation details for ApiRouter.
// NOT part of the public API. Only included by ApiRouter.cc and ApiRouterRoutes.cc.
// Eliminates code duplication between the two translation units (DEBT-P1-05).
#pragma once

#include <memory>
#include <string>
#include <system_error>

#include "api/ApiRouter.h"
#include "nlohmann/json.hpp"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/Log.h"
#include "util/StringUtil.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {

// Auth type shorthand constants (replacing #define AUTH / NOAUTH)
constexpr auto kAuth   = InterfaceMsgAuthType::Mtk;
constexpr auto kNoAuth = InterfaceMsgAuthType::None;

namespace detail {

    inline std::string ErroResult(const util::ErrorMessage& e, std::error_condition& errc) {
        LOG_ERRO("{}", e.what());
        errc = e.GetValue();
        MsgSendHead retData{};
        retData.resCode = kServerRspFailed;
        MsgResBase resMsg;
        resMsg.msgCode = std::to_string(errc.value());
        resMsg.msgText = e.what();
        retData.resMsg.push_back(resMsg);
        return nlohmann::json(retData).dump();
    }

    inline std::string ErroResult(const char* errMsg, std::error_condition& errc) {
        LOG_ERRO("{}", errMsg);
        MsgSendHead retData{};
        retData.resCode = kServerRspFailed;
        MsgResBase resMsg;
        resMsg.msgCode = std::to_string(errc.value());
        resMsg.msgText = errMsg;
        retData.resMsg.push_back(resMsg);
        return nlohmann::json(retData).dump();
    }

    inline std::string ErroResult(const nlohmann::json::exception& e, std::error_condition& errc) {
        LOG_ERRO("{}", e.what());
        errc = util::ErrorEnum::ParameterException;
        MsgSendHead retData{};
        retData.resCode = kServerRspFailed;
        MsgResBase resMsg;
        resMsg.msgCode = std::to_string(errc.value());
        resMsg.msgText = errc.message();
        retData.resMsg.push_back(resMsg);
        return nlohmann::json(retData).dump();
    }

    /// Generic JSON request dispatch: parse request -> call Handler -> build response
    template <typename Ret, typename Par, typename Handler>
    std::string DispatchJson(MessageFromType from, Handler& handler, const std::string& jsonStr,
                             std::error_condition& errc) {
        Par p{};
        try {
            if (!jsonStr.empty()) {
                auto j = nlohmann::json::parse(jsonStr);
                j.get_to(p);
            }
            p.messageFrom = from;
            auto ret      = handler.Handle(std::move(p), errc);
            if (errc == util::ErrorEnum::Success) {
                ret.resCode = kServerRspSuccess;
                MsgResBase resMsg;
                resMsg.msgCode = std::to_string(errc.value());
                resMsg.msgText = "操作成功";
                ret.resMsg.push_back(resMsg);
            } else {
                ret.resCode = kServerRspFailed;
                MsgResBase resMsg;
                resMsg.msgCode = std::to_string(errc.value());
                resMsg.msgText = errc.message();
                ret.resMsg.push_back(resMsg);
            }
            return nlohmann::json(ret).dump(-1, ' ', false, nlohmann::json::error_handler_t::ignore);
        } catch (const nlohmann::json::exception& e) {
            return ErroResult(e, errc);
        } catch (const util::ErrorMessage& e) {
            return ErroResult(e, errc);
        } catch (const std::exception& e) {
            return ErroResult(e.what(), errc);
        }
    }

}  // namespace detail
}  // namespace cosmo

// ---------------------------------------------------------------------------
// Route registration macros (must remain as macros due to ## token pasting)
// ---------------------------------------------------------------------------
#define ROUTE(url, auth, handler, NS, X)                                                                     \
    url_map_[util::ToLower(url #X)] = {auth,                                                                 \
                                       [this](const std::string& jsonStr, std::error_condition& errc) {      \
                                           return detail::DispatchJson<NS::Msg##X##Send, NS::Msg##X##Recv>(  \
                                               GetMessageFrom(), *handler, jsonStr, errc);                   \
                                       }}

#define ROUTE_CORE(url, X)                                                                                   \
    url_map_[util::ToLower(url #X)] = {InterfaceMsgAuthType::None,                                           \
                                       [this](const std::string& jsonStr, std::error_condition& errc) {      \
                                           return detail::DispatchJson<Msg##X##Send, Msg##X##Recv>(          \
                                               GetMessageFrom(), *handler_, jsonStr, errc);                  \
                                       }}
