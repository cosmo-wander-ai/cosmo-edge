#pragma once
#include <system_error>

#include "service/network/dto/NetworkDto.h"

namespace cosmo::service {
class INetworkConfig;
}  // namespace cosmo::service

namespace cosmo {

class MessageNetworkHandler {
public:
    explicit MessageNetworkHandler(service::INetworkConfig& network_config);
    Network::MsgQueryNetCardSend Handle(Network::MsgQueryNetCardRecv&& data, std::error_condition& errc);
    Network::MsgModifyNetCardSend Handle(Network::MsgModifyNetCardRecv&& data, std::error_condition& errc);
    Network::MsgQueryNetDnsSend Handle(Network::MsgQueryNetDnsRecv&& data, std::error_condition& errc);
    Network::MsgModifyNetDnsSend Handle(Network::MsgModifyNetDnsRecv&& data, std::error_condition& errc);
    Network::MsgNetworkQualityCheckSend Handle(Network::MsgNetworkQualityCheckRecv&& data,
                                               std::error_condition& errc);
    Network::MsgIpAccessibleCheckSend Handle(Network::MsgIpAccessibleCheckRecv&& data,
                                             std::error_condition& errc);

private:
    service::INetworkConfig& network_config_;
};

}  // namespace cosmo
