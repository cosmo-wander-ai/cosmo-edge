// MessageNetworkHandler — Message Network Handler implementation.

#include "api/MessageNetworkHandler.h"

#include "service/network/INetworkConfig.h"
#include "util/ErrorCode.h"
#include "util/Log.h"

namespace cosmo {

static constexpr const char* kTag = "MessageNetworkHandler";

namespace {

    /// Convert platform::NetCardInfo to DTO MsgNetCardInfo for API response.
    Network::MsgNetCardInfo ToMsgNetCardInfo(const platform::NetCardInfo& card) {
        Network::MsgNetCardInfo info;
        info.mainCard = card.is_main;
        info.dhcp     = card.dhcp;
        info.ethName  = card.is_main ? platform::kNetworkMainCardName : platform::kNetworkSubCardName;
        info.ipAddr   = card.ip_addr;
        info.netMask  = card.net_mask;
        info.gateway  = card.gateway;
        info.mac      = card.mac;
        return info;
    }

}  // namespace

MessageNetworkHandler::MessageNetworkHandler(service::INetworkConfig& network_config)
    : network_config_(network_config) {}

Network::MsgQueryNetCardSend MessageNetworkHandler::Handle(Network::MsgQueryNetCardRecv&& /*data*/,
                                                           std::error_condition& /*errc*/) {
    Network::MsgQueryNetCardSend retData{};

    auto cards = network_config_.GetCardRealInfos();
    for (auto& card : cards) {
        retData.resData.netCardList.push_back(ToMsgNetCardInfo(card));
    }

    return retData;
}

Network::MsgModifyNetCardSend MessageNetworkHandler::Handle(Network::MsgModifyNetCardRecv&& data,
                                                            std::error_condition& /*errc*/) {
    Network::MsgModifyNetCardSend retData{};
    platform::NetCardInfo info;
    info.dhcp     = data.netCard.dhcp;
    info.is_main  = data.netCard.mainCard;
    info.eth_name = info.is_main ? platform::kNetworkMainEthName : platform::kNetworkSubEthName;
    info.ip_addr  = data.netCard.ipAddr;
    info.net_mask = data.netCard.netMask;
    info.gateway  = data.netCard.gateway;
    network_config_.ApplyCardInfoAsync(info);

    return retData;
}

Network::MsgQueryNetDnsSend MessageNetworkHandler::Handle(Network::MsgQueryNetDnsRecv&& /*data*/,
                                                          std::error_condition& /*errc*/) {
    Network::MsgQueryNetDnsSend retData{};

    auto dnss = network_config_.GetCfgDns();
    if (dnss.size() >= 1)
        retData.resData.dns1 = dnss[0];
    if (dnss.size() >= 2)
        retData.resData.dns2 = dnss[1];

    return retData;
}

Network::MsgModifyNetDnsSend MessageNetworkHandler::Handle(Network::MsgModifyNetDnsRecv&& data,
                                                           std::error_condition& errc) {
    Network::MsgModifyNetDnsSend retData{};
    std::vector<std::string> dnss;
    if (!data.dns1.empty())
        dnss.push_back(data.dns1);
    if (!data.dns2.empty())
        dnss.push_back(data.dns2);
    if (!network_config_.SetDnss(dnss)) {
        errc = util::ErrorEnum::Failed;
    }

    return retData;
}

Network::MsgNetworkQualityCheckSend MessageNetworkHandler::Handle(Network::MsgNetworkQualityCheckRecv&& data,
                                                                  std::error_condition& /*errc*/) {
    Network::MsgNetworkQualityCheckSend retData{};

    auto result = network_config_.ProbeNetworkQuality(data.ip, data.packetSize);
    if (result.success) {
        retData.resData.lostRate       = result.lost_rate;
        retData.resData.averageLatency = result.average_latency;
    } else {
        retData.resCode = static_cast<int>(util::ErrorEnum::IpProbeFailed);
    }

    return retData;
}

Network::MsgIpAccessibleCheckSend MessageNetworkHandler::Handle(Network::MsgIpAccessibleCheckRecv&& data,
                                                                std::error_condition& /*errc*/) {
    Network::MsgIpAccessibleCheckSend retData{};
    retData.resData.ip = data.ip;

    if (network_config_.IsIpAccessible(data.ip)) {
        retData.resData.accessible = 1;
    } else {
        retData.resCode = static_cast<int>(util::ErrorEnum::IpProbeFailed);
    }

    return retData;
}

}  // namespace cosmo
