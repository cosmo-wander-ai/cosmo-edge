// DTO types for device discovery multicast protocol.
// JSON field names MUST match the external search tool protocol — do NOT rename JSON fields.
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "service/infra/dto/InfraMsgTypes.h"
#include "service/system/dto/SystemMsgTypes.h"

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

}  // namespace cosmo::service
