// DTO types for device discovery multicast protocol.
// JSON field names MUST match the external search tool protocol — do NOT rename JSON fields.
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "service/infra/dto/InfraMsgTypes.h"
#include "service/system/dto/SystemMsgTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo::service {

// Base message fields shared by all discovery commands.
struct DiscoveryBaseMsg {
    std::string from;  // Sender IP (DHCP fallback: unicast to peer)
    std::string cmd;
    std::string type;
    std::string reqId;
    friend void to_json(nlohmann::json& j, const DiscoveryBaseMsg& v);
    friend void from_json(const nlohmann::json& j, DiscoveryBaseMsg& v);
};

// Vague message with optional device SN filter.
struct DiscoveryVagueMsg : DiscoveryBaseMsg {
    std::string deviceSn;
};

void to_json(nlohmann::json& j, const DiscoveryVagueMsg& v);
void from_json(const nlohmann::json& j, DiscoveryVagueMsg& v);

// Probe request (alias).
using DiscoveryProbeRecv = DiscoveryBaseMsg;

// Common send-side header.
struct DiscoverySendHead {
    int resCode{0};
    std::string resMsg;
    friend void to_json(nlohmann::json& j, const DiscoverySendHead& v);
    friend void from_json(const nlohmann::json& j, DiscoverySendHead& v);
};

// Probe response.
struct DiscoveryProbeSend : DiscoveryBaseMsg, DiscoverySendHead {
    struct ResData {
        std::vector<cosmo::MsgNetCardInfo> netCardList;
        std::vector<cosmo::DeviceInfo> devInfoList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const DiscoveryProbeSend& v);
void from_json(const nlohmann::json& j, DiscoveryProbeSend& v);

// ModifyNetCard request.
struct ModifyNetCardRequest : DiscoveryVagueMsg {
    std::string passwd;
    cosmo::MsgNetCardInfo netCard;
};

void to_json(nlohmann::json& j, const ModifyNetCardRequest& v);
void from_json(const nlohmann::json& j, ModifyNetCardRequest& v);

// ModifyNetCard response.
struct ModifyNetCardResponse : DiscoveryBaseMsg, DiscoverySendHead {};

void to_json(nlohmann::json& j, const ModifyNetCardResponse& v);
void from_json(const nlohmann::json& j, ModifyNetCardResponse& v);

// Device hardware info.
struct DeviceHWInfo {
    std::string devSn;
    std::string devType;
    std::string hwVersion;
    friend void to_json(nlohmann::json& j, const DeviceHWInfo& v);
    friend void from_json(const nlohmann::json& j, DeviceHWInfo& v);
};

// HW info write request.
struct HWInfoWriteRequest : public DiscoveryVagueMsg {
    DeviceHWInfo devHWInfo;
};

void to_json(nlohmann::json& j, const HWInfoWriteRequest& v);
void from_json(const nlohmann::json& j, HWInfoWriteRequest& v);

// HW info write response.
struct HWInfoWriteResponse : public DiscoveryBaseMsg, DiscoverySendHead {
    DeviceHWInfo devHWInfo;
};

void to_json(nlohmann::json& j, const HWInfoWriteResponse& v);
void from_json(const nlohmann::json& j, HWInfoWriteResponse& v);

// Auth code info (shared by modify and query responses).
struct AuthCodeInfoSend : public DiscoverySendHead {
    struct Server {
        std::string name;
        int type{0};
        int number{0};
        int validDays{0};
        int authorDays{0};
        std::string validDate;
        friend void to_json(nlohmann::json& j, const Server& v);
        friend void from_json(const nlohmann::json& j, Server& v);
    };

    struct ResData {
        int status{0};
        std::vector<Server> serverList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const AuthCodeInfoSend& v);
void from_json(const nlohmann::json& j, AuthCodeInfoSend& v);

// ModifyAuthCode request.
struct AuthCodeModifyRequest : public DiscoveryVagueMsg {
    std::string authCode;
};

void to_json(nlohmann::json& j, const AuthCodeModifyRequest& v);
void from_json(const nlohmann::json& j, AuthCodeModifyRequest& v);

// ModifyAuthCode response.
struct AuthCodeModifyResponse : public DiscoveryBaseMsg, AuthCodeInfoSend {};

void to_json(nlohmann::json& j, const AuthCodeModifyResponse& v);
void from_json(const nlohmann::json& j, AuthCodeModifyResponse& v);

// QueryAuthMessage request (empty body).
struct AuthStatusQueryRequest : public DiscoveryVagueMsg {};

// QueryAuthMessage response.
struct AuthStatusQueryResponse : public DiscoveryBaseMsg, AuthCodeInfoSend {};

void to_json(nlohmann::json& j, const AuthStatusQueryResponse& v);
void from_json(const nlohmann::json& j, AuthStatusQueryResponse& v);

// Internal: queryAuthorMessage HTTP request/response.
struct AuthQueryHttpRequest : public cosmo::MsgRecvHead {};

struct AuthQueryHttpResponse : public AuthCodeInfoSend {};

// Internal: login HTTP request/response.
struct LoginHttpRequest : public cosmo::MsgRecvHead {
    int language{0};
    cosmo::util::String<1, 16> user;
    cosmo::util::String<32, 32> passwd;
};

void to_json(nlohmann::json& j, const LoginHttpRequest& v);
void from_json(const nlohmann::json& j, LoginHttpRequest& v);

struct LoginHttpResponse : public DiscoverySendHead {
    struct ResData {
        std::string token;
        std::string deviceSn;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const LoginHttpResponse& v);
void from_json(const nlohmann::json& j, LoginHttpResponse& v);

// Internal: modifyAuthorCode HTTP request.
struct AuthCodeHttpRequest : public cosmo::MsgRecvHead {
    std::string authorCode;
};

void to_json(nlohmann::json& j, const AuthCodeHttpRequest& v);
void from_json(const nlohmann::json& j, AuthCodeHttpRequest& v);

// Internal: password file structure.
struct PasswordFile {
    struct UserPasswd {
        std::string admin;
        friend void to_json(nlohmann::json& j, const UserPasswd& v);
        friend void from_json(const nlohmann::json& j, UserPasswd& v);
    } userPasswd;
    friend void to_json(nlohmann::json& j, const PasswordFile& v);
    friend void from_json(const nlohmann::json& j, PasswordFile& v);
};

}  // namespace cosmo::service
