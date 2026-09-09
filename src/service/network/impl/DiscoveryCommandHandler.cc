// DiscoveryCommandHandler.cc — Production multicast probe handler.

#include <nlohmann/json.hpp>

#include "service/detail/ServiceRegistry.h"
#include "service/network/INetworkService.h"
#include "service/network/impl/DeviceDiscoveryServiceImpl.h"
#include "service/system/IDeviceHardware.h"
#include "util/ErrorCode.h"
#include "util/Log.h"
#include "util/Version.h"

namespace cosmo::service {

void DeviceDiscoveryServiceImpl::HandleProbe(DiscoveryProbeRecv&& data) {
    DiscoveryProbeSend ret_data{};
    ret_data.cmd   = data.cmd;
    ret_data.type  = "ack";
    ret_data.reqId = data.reqId;

    std::error_condition errc{};
    auto cards = ServiceRegistry::Instance().Get<INetworkService>().GetCardRealInfos();
    for (auto& card : cards) {
        MsgNetCardInfo info;
        info.mainCard = card.is_main;
        info.dhcp     = card.dhcp;
        info.ethName  = card.eth_name;
        info.ipAddr   = card.ip_addr;
        info.netMask  = card.net_mask;
        info.gateway  = card.gateway;
        ret_data.resData.netCardList.push_back(info);
    }

    if (ret_data.resData.netCardList.empty()) {
        LOG_ERRO("{}", "get netCardList failed");
        errc = util::ErrorEnum::Failed;
    } else {
        ret_data.resData.devInfoList.push_back(
            {"deviceType", "设备型号", ServiceRegistry::Instance().Get<IDeviceHardware>().GetDevModel()});
        ret_data.resData.devInfoList.push_back({"softwareVersion", "软件版本", util::GetAbbrVersion()});
        ret_data.resData.devInfoList.push_back(
            {"deviceSn", "设备SN", ServiceRegistry::Instance().Get<IDeviceHardware>().GetDevSn()});
    }

    ret_data.resCode = errc.value();
    ret_data.resMsg  = errc.message();
    SendMessage(std::move(nlohmann::json(ret_data).dump()), data.from);
}

}  // namespace cosmo::service
