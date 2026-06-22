// Network DTO definitions (extracted from MessageNetworkHandler.h)

#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace Network {
    struct MsgNetCardInfo {
        int mainCard{1};
        int dhcp{0};
        std::string ethName;
        std::string ipAddr;
        std::string netMask;
        std::string gateway;
        std::string mac;

        friend void to_json(nlohmann::json& j, const MsgNetCardInfo& v);
        friend void from_json(const nlohmann::json& j, MsgNetCardInfo& v);
    };

    struct MsgQueryNetCardRecv : public MsgRecvHead {};

    // Query network card response
    struct MsgQueryNetCardSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgNetCardInfo> netCardList;
            std::string dns1;
            std::string dns2;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryNetCardSend& v);
    void from_json(const nlohmann::json& j, MsgQueryNetCardSend& v);

    struct MsgModifyNetCardRecv : public MsgRecvHead {
        MsgNetCardInfo netCard;
    };

    void to_json(nlohmann::json& j, const MsgModifyNetCardRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyNetCardRecv& v);

    // Modify network card response
    struct MsgModifyNetCardSend : public MsgSendHead {};

    struct MsgQueryNetDnsRecv : public MsgRecvHead {};

    // Query DNS response
    struct MsgQueryNetDnsSend : public MsgSendHead {
        struct ResData {
            std::string dns1;
            std::string dns2;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgQueryNetDnsSend& v);
    void from_json(const nlohmann::json& j, MsgQueryNetDnsSend& v);

    struct MsgModifyNetDnsRecv : public MsgRecvHead {
        std::string dns1;
        std::string dns2;
    };

    void to_json(nlohmann::json& j, const MsgModifyNetDnsRecv& v);
    void from_json(const nlohmann::json& j, MsgModifyNetDnsRecv& v);

    // Modify DNS response
    struct MsgModifyNetDnsSend : public MsgSendHead {};

    struct MsgNetworkQualityCheckRecv : public MsgRecvHead {
        std::string ip;
        util::RangeInt<64, 10000> packetSize;
    };

    void to_json(nlohmann::json& j, const MsgNetworkQualityCheckRecv& v);
    void from_json(const nlohmann::json& j, MsgNetworkQualityCheckRecv& v);

    // Network quality check response
    struct MsgNetworkQualityCheckSend : public MsgSendHead {
        struct ResData {
            float lostRate{100.0};
            float averageLatency;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgNetworkQualityCheckSend& v);
    void from_json(const nlohmann::json& j, MsgNetworkQualityCheckSend& v);

    // Query whether IP address is accessible
    struct MsgIpAccessibleCheckRecv : public MsgRecvHead {
        util::String<0, 128> ip;  // IP address and domain name to query
    };

    void to_json(nlohmann::json& j, const MsgIpAccessibleCheckRecv& v);
    void from_json(const nlohmann::json& j, MsgIpAccessibleCheckRecv& v);

    // IP accessibility check response
    struct MsgIpAccessibleCheckSend : public MsgSendHead {
        struct ResData {
            std::string ip;
            int accessible{0};
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgIpAccessibleCheckSend& v);
    void from_json(const nlohmann::json& j, MsgIpAccessibleCheckSend& v);
}  // namespace Network
}  // namespace cosmo
