#pragma once

#include <nlohmann/json_fwd.hpp>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "service/network/INetworkConfig.h"

namespace cosmo::service {

struct NetWorkInfo {
    cosmo::platform::NetCardInfo main;
    cosmo::platform::NetCardInfo sub;
    std::string dns1{"114.114.114.114"};
    std::string dns2{""};
    friend void to_json(nlohmann::json& j, const NetWorkInfo& v);
    friend void from_json(const nlohmann::json& j, NetWorkInfo& v);
};

class NetworkConfigServiceImpl final : public INetworkConfig {
public:
    NetworkConfigServiceImpl();
    ~NetworkConfigServiceImpl() override;

    NetworkConfigServiceImpl(const NetworkConfigServiceImpl&)            = delete;
    NetworkConfigServiceImpl& operator=(const NetworkConfigServiceImpl&) = delete;

    // INetworkConfig
    void Init() override;
    cosmo::platform::NetCardInfo GetCardRealInfo(bool main = true) override;
    std::vector<cosmo::platform::NetCardInfo> GetCardRealInfos() override;
    bool SetCardInfo(const cosmo::platform::NetCardInfo& info) override;
    void ApplyCardInfoAsync(const cosmo::platform::NetCardInfo& info) override;
    std::vector<std::string> GetCfgDns() override;
    bool SetDnss(std::vector<std::string> dnss) override;
    bool SearchSetNewInfo(cosmo::platform::NetCardInfo& netCard, const std::string& dns1,
                          const std::string& dns2) override;
    std::vector<NetCardView> GetNetCards() override;
    std::string GetHostIpAddress() override;
    PingQualityResult ProbeNetworkQuality(const std::string& ip, int packet_size) override;
    bool IsIpAccessible(const std::string& ip) override;

private:
    void SetToDefault();
    void LoadCfg();
    void GetMacs();
    cosmo::platform::NetCardInfo GetCardInfo(bool main = true);
    void UpdateNetCardCfg(const cosmo::platform::NetCardInfo& info);
    void UpdateNetDnsCfg(std::vector<std::string> dnss);

private:
    std::thread apply_thread_;
    std::shared_mutex mtx_;
    std::string conf_file_name_{"netCradConf.json"};           // Local network config
    std::string conf_search_file_name_{"netCardSearch.json"};  // LAN search config
    NetWorkInfo net_card_info_;
    std::vector<std::pair<std::string, std::string>> mac_infos_;
    std::string main_mac_{""};
    std::string sub_mac_{""};
};

}  // namespace cosmo::service
