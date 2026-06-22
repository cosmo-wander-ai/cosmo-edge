// NetworkConfigServiceImpl — Network Config Service Impl implementation.

#include "service/network/impl/NetworkConfigServiceImpl.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "platform/NetCardOp.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IDeviceInfoService.h"
#include "service/system/ISystemOperationService.h"
#include "util/Exec.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/SafeParse.h"
#include "util/TimingConstants.h"

#define DO_NETCARD_ALL

namespace cosmo::service {

NetworkConfigServiceImpl::NetworkConfigServiceImpl() = default;

NetworkConfigServiceImpl::~NetworkConfigServiceImpl() {
    if (apply_thread_.joinable()) {
        apply_thread_.join();
    }
}

void NetworkConfigServiceImpl::GetMacs() {
    if (mac_infos_.empty() || main_mac_.empty() || sub_mac_.empty()) {
        mac_infos_ = ServiceRegistry::Instance().Get<IDeviceInfoService>().GetMacs();
        for (const auto& macInfo : mac_infos_) {
            if (macInfo.first == platform::kNetworkMainEthName) {
                main_mac_ = macInfo.second;
                continue;
            }
            if (macInfo.first == platform::kNetworkSubEthName) {
                sub_mac_ = macInfo.second;
                continue;
            }
        }
    }
}

bool NetworkConfigServiceImpl::SearchSetNewInfo(platform::NetCardInfo& netCard, const std::string& dns1,
                                                const std::string& dns2) {
    NetWorkInfo networkInfoSearch;
    auto search_cfg_path =
        (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_search_file_name_).string();
    if (!util::LoadStructFromJsonFile(search_cfg_path, networkInfoSearch)) {
        LOG_ERRO("Failed to load search network config from {}", search_cfg_path);
    }
    if (netCard.is_main) {
        networkInfoSearch.main = netCard;
    } else {
        networkInfoSearch.sub = netCard;
    }
    networkInfoSearch.dns1 = dns1;
    networkInfoSearch.dns2 = dns2;

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
    if (!util::SaveStructToJsonFile(path, networkInfoSearch)) {
        LOG_ERRO("Failed to save search network config to {}", path);
    }

    return true;
}

void NetworkConfigServiceImpl::SetToDefault() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    net_card_info_.main.eth_name = platform::kNetworkMainEthName;
    net_card_info_.main.dhcp     = 0;
    net_card_info_.main.ip_addr  = "192.168.100.1";
    net_card_info_.main.net_mask = "255.255.255.0";

    net_card_info_.sub.eth_name = platform::kNetworkSubEthName;
    net_card_info_.sub.dhcp     = 0;
    net_card_info_.sub.ip_addr  = "192.168.1.18";
    net_card_info_.sub.net_mask = "255.255.255.0";

    net_card_info_.dns1 = "114.114.114.114";
    net_card_info_.dns2 = "";
}

void NetworkConfigServiceImpl::LoadCfg() {
    NetWorkInfo networkInfo;
    time_t cfg_time_point = 0;
    NetWorkInfo networkInfoSearch;
    time_t cfg_search_time_point = 0;

    auto main_cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
    auto search_cfg_path =
        (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_search_file_name_).string();
    auto ret = util::LoadStructFromJsonFile(main_cfg_path, networkInfo);
    if (ret) {
        cfg_time_point = util::GetFileModifyTime(main_cfg_path);
    }
    auto ret_search = util::LoadStructFromJsonFile(search_cfg_path, networkInfoSearch);
    if (ret_search) {
        cfg_search_time_point = util::GetFileModifyTime(search_cfg_path);
    }
    // Both failed, use default config
    if ((!ret) && (!ret_search)) {
        SetToDefault();

        auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
        if (!util::SaveStructToJsonFile(path, net_card_info_)) {
            LOG_ERRO("Failed to save default network config to {}", path);
        }
        LOG_INFO("{}", "NetworkCfg Load Failed. Use Default, Maybe First Start.");
        return;
    }

    // Both succeeded
    if ((ret) && (ret_search)) {
        // Normal case after LAN search
        if (cfg_search_time_point > cfg_time_point) {
            // LAN search config is newer; adopt it and update config file
            net_card_info_ = networkInfoSearch;

            auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
            if (!util::SaveStructToJsonFile(path, net_card_info_)) {
                LOG_ERRO("Failed to save updated network config to {}", path);
            }
            LOG_INFO("{}", "NetworkCfg Search Cfg Updated. Network Info Update.");
            return;
        } else {
            net_card_info_ = networkInfo;
            return;
        }
    }

    // Only LAN config loaded successfully — should be impossible; indicates a problem
    if (ret_search) {
        net_card_info_ = networkInfoSearch;

        auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
        if (!util::SaveStructToJsonFile(path, net_card_info_)) {
            LOG_ERRO("Failed to save search-only network config to {}", path);
        }
        LOG_WARN("{}", "NetworkCfg Search Cfg Only [[[ATTENTION]]]. Network Info Update.");
        return;
    }

    // Only local config loaded successfully; use local config
    net_card_info_ = networkInfo;
    return;
}

std::vector<std::string> NetworkConfigServiceImpl::GetCfgDns() {
    std::vector<std::string> dnss;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    if (!net_card_info_.dns1.empty()) {
        dnss.push_back(net_card_info_.dns1);
    }
    if (!net_card_info_.dns2.empty()) {
        dnss.push_back(net_card_info_.dns2);
    }
    return dnss;
}

platform::NetCardInfo NetworkConfigServiceImpl::GetCardInfo(bool main) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    if (main) {
        auto info    = net_card_info_.main;
        info.is_main = true;
        if (!net_card_info_.dns1.empty()) {
            info.dnss.push_back(net_card_info_.dns1);
        }
        if (!net_card_info_.dns2.empty()) {
            info.dnss.push_back(net_card_info_.dns2);
        }
        return info;
    } else {
        return net_card_info_.sub;
    }
}

platform::NetCardInfo NetworkConfigServiceImpl::GetCardRealInfo(bool main) {
    GetMacs();
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto eth_name = platform::kNetworkMainEthName;
    if (!main) {
        eth_name = platform::kNetworkSubEthName;
    }
    auto real_info    = platform::GetIpInfo(eth_name);
    real_info.is_main = main;
    if (main) {
        real_info.mac      = main_mac_;
        real_info.eth_name = platform::kNetworkMainEthName;
        auto info          = net_card_info_.main;
        if (real_info.ip_addr.empty()) {
            real_info.ip_addr  = info.ip_addr;
            real_info.net_mask = info.net_mask;
            real_info.gateway  = info.gateway;
        }
        real_info.dhcp = info.dhcp;
    } else {
        real_info.mac      = sub_mac_;
        real_info.eth_name = platform::kNetworkSubEthName;
        auto info          = net_card_info_.sub;
        if (real_info.ip_addr.empty()) {
            real_info.ip_addr  = info.ip_addr;
            real_info.net_mask = info.net_mask;
        }
        real_info.dhcp = info.dhcp;
    }
    return real_info;
}

std::vector<platform::NetCardInfo> NetworkConfigServiceImpl::GetCardRealInfos() {
    std::vector<platform::NetCardInfo> infos;
    infos.push_back(GetCardRealInfo(true));
    infos.push_back(GetCardRealInfo(false));
    return infos;
}

void NetworkConfigServiceImpl::UpdateNetCardCfg(const platform::NetCardInfo& info) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (info.is_main) {
        net_card_info_.main.dhcp     = info.dhcp;
        net_card_info_.main.ip_addr  = info.ip_addr;
        net_card_info_.main.net_mask = info.net_mask;
        net_card_info_.main.gateway  = info.gateway;
    } else {
        net_card_info_.sub.dhcp     = info.dhcp;
        net_card_info_.sub.ip_addr  = info.ip_addr;
        net_card_info_.sub.net_mask = info.net_mask;
        net_card_info_.sub.gateway  = info.gateway;
    }

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
    if (!util::SaveStructToJsonFile(path, net_card_info_)) {
        LOG_ERRO("Failed to save network card config to {}", path);
    }
}

void NetworkConfigServiceImpl::UpdateNetDnsCfg(std::vector<std::string> dnss) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (dnss.empty()) {
        net_card_info_.dns1 = "";
        net_card_info_.dns2 = "";
    } else if (dnss.size() == 1) {
        net_card_info_.dns1 = dnss.at(0);
        net_card_info_.dns2 = "";
    } else {
        net_card_info_.dns1 = dnss.at(0);
        net_card_info_.dns2 = dnss.at(1);
    }

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / conf_file_name_).string();
    if (!util::SaveStructToJsonFile(path, net_card_info_)) {
        LOG_ERRO("Failed to save DNS config to {}", path);
    }
}

bool NetworkConfigServiceImpl::SetCardInfo(const platform::NetCardInfo& info) {
    int ret_code = 1;
#ifdef DO_NETCARD_ALL
    LOG_INFO("Set {}/{} ", info.eth_name, info.ip_addr);
    UpdateNetCardCfg(info);
    ret_code = platform::DoNetCards(GetCardInfo(true), GetCardInfo(false));
    LOG_INFO("Set NetCards After Config Update Ret {}", ret_code);
#else
    auto effect_info = info;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        if (effect_info.is_main) {
            effect_info.is_dhcp_change = (net_card_info_.main.dhcp != effect_info.dhcp);
        } else {
            effect_info.is_dhcp_change = (net_card_info_.sub.dhcp != effect_info.dhcp);
        }
    }
    auto res        = platform::DoNetCard(effect_info);
    ret_code        = static_cast<int>(res.error_code);
    auto ret_string = res.result_data;
    LOG_INFO("Set {}/{} Get Ret {}:{}", info.eth_name, info.ip_addr, ret_code, ret_string);
#endif
    if (0 == ret_code) {
        UpdateNetCardCfg(info);
        return true;
    }
    if (1 == ret_code) {
        return true;
    }
    return false;
}

void NetworkConfigServiceImpl::ApplyCardInfoAsync(const platform::NetCardInfo& info) {
    if (apply_thread_.joinable()) {
        apply_thread_.join();
    }
    apply_thread_ = std::thread([this, info]() {
        std::this_thread::sleep_for(timing::kOneSecondInterval);
        if (SetCardInfo(info)) {
            ServiceRegistry::Instance().Get<ISystemOperationService>().RebootDevice("Interface Reboot");
        } else {
            LOG_ERRO("Set netcard {} failed, skip interface reboot", info.eth_name);
        }
    });
}

bool NetworkConfigServiceImpl::SetDnss(std::vector<std::string> dnss) {
    platform::DnsEffect(dnss);
    UpdateNetDnsCfg(dnss);
    return true;
}

void NetworkConfigServiceImpl::Init() {
    GetMacs();
#ifdef SOPHON_SHARED_NETWORK
    LOG_INFO("{}", "SOPHON_SHARED_NETWORK enabled, skip local network configuration.");
    return;
#endif
    LoadCfg();
    platform::DnsEffect(GetCfgDns());
    int ret_code = 0;

#ifdef DO_NETCARD_ALL
    ret_code = DoNetCards(GetCardInfo(true), GetCardInfo(false));
    LOG_INFO("Network Init DoNetCards Ret {}", ret_code);
#else
    auto res_main = platform::DoNetCard(GetCardInfo(true));
    ret_code      = static_cast<int>(res_main.error_code);
    auto res_sub  = platform::DoNetCard(GetCardInfo(false));
    ret_code      = static_cast<int>(res_sub.error_code);
#endif
}

std::vector<NetCardView> NetworkConfigServiceImpl::GetNetCards() {
    auto cards = GetCardRealInfos();
    std::vector<NetCardView> result;
    result.reserve(cards.size());
    for (auto& card : cards) {
        NetCardView view;
        view.isMain  = card.is_main;
        view.dhcp    = (card.dhcp != 0);
        view.ethName = card.eth_name;
        view.ipAddr  = card.ip_addr;
        view.netMask = card.net_mask;
        view.gateway = card.gateway;
        result.push_back(std::move(view));
    }
    return result;
}

std::string NetworkConfigServiceImpl::GetHostIpAddress() {
    auto cards = GetNetCards();
    auto it    = std::find_if(cards.begin(), cards.end(), [](const auto& card) { return card.isMain; });
    if (it != cards.end()) {
        return it->ipAddr;
    }
    return cards.empty() ? "" : cards[0].ipAddr;
}

INetworkConfig::PingQualityResult NetworkConfigServiceImpl::ProbeNetworkQuality(const std::string& ip,
                                                                                int packet_size) {
    PingQualityResult result{};

    if (!util::IsHostnameSafe(ip)) {
        LOG_ERRO("Invalid IP/hostname: {}", ip);
        return result;
    }

    constexpr int kPingCount = 4;
    std::string cmd = "ping " + util::ShellEscape(ip) + " -c " + std::to_string(kPingCount) + " -s " +
                      std::to_string(packet_size);
    LOG_INFO("{}", cmd);

    std::vector<std::string> lines;
    if (util::Exec(cmd, lines, true) != 0) {
        LOG_ERRO("Cmd:{} exec failed", cmd);
        return result;
    }

    const std::string key{"time="};
    int received   = 0;
    float total_ms = 0.0f;

    for (const auto& line : lines) {
        size_t pos = line.find(key);
        LOG_INFO("{} ", line);
        if (pos < line.length()) {
            std::istringstream iss(line.substr(pos + key.size()));
            std::string latency_str;
            iss >> latency_str;
            received += 1;
            total_ms += util::ParseFloat(latency_str);
        }
    }

    LOG_INFO("Receive {} Packets Cost {} ms", received, total_ms);
    if (received > 0 && received <= kPingCount) {
        result.lost_rate       = 100.0f * static_cast<float>(kPingCount - received) / kPingCount;
        result.average_latency = total_ms / static_cast<float>(received);
        result.success         = true;
    }

    return result;
}

bool NetworkConfigServiceImpl::IsIpAccessible(const std::string& ip) {
    if (!util::IsHostnameSafe(ip)) {
        LOG_ERRO("Invalid IP/hostname: {}", ip);
        return false;
    }

    auto latency = util::PingLatency(ip);
    if (latency.empty()) {
        LOG_ERRO("PingLatency failed for IP: {}", ip);
        return false;
    }

    LOG_INFO("PingLatency for {} = {}", ip, latency);
    return true;
}

}  // namespace cosmo::service

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::service {
void from_json(const nlohmann::json& j, NetWorkInfo& v) {
    if (j.contains("main") && !j["main"].is_null())
        j.at("main").get_to(v.main);
    if (j.contains("sub") && !j["sub"].is_null())
        j.at("sub").get_to(v.sub);
    if (j.contains("dns1") && !j["dns1"].is_null())
        j.at("dns1").get_to(v.dns1);
    if (j.contains("dns2") && !j["dns2"].is_null())
        j.at("dns2").get_to(v.dns2);
}

void to_json(nlohmann::json& j, const NetWorkInfo& v) {
    j["main"] = v.main;
    j["sub"]  = v.sub;
    j["dns1"] = v.dns1;
    j["dns2"] = v.dns2;
}

}  // namespace cosmo::service
