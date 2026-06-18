#pragma once

#include <functional>
#include <memory>

#include "service/network/INetworkService.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::service {

class HttpLifecycleServiceImpl;
class MqttLifecycleServiceImpl;
class NetworkConfigServiceImpl;

class NetworkServiceImpl final : public INetworkService {
public:
    using DispatcherFactory = std::function<std::unique_ptr<cosmo::IRequestDispatcher>()>;

    /// @param http_dispatcher_factory  Factory for HTTP request dispatchers (one per thread)
    /// @param mqtt_dispatcher_factory  Factory for the MQTT request dispatcher
    NetworkServiceImpl(DispatcherFactory http_dispatcher_factory, DispatcherFactory mqtt_dispatcher_factory);
    ~NetworkServiceImpl() override;

    NetworkServiceImpl(const NetworkServiceImpl&)            = delete;
    NetworkServiceImpl& operator=(const NetworkServiceImpl&) = delete;

    // IHttpLifecycle
    void InitHttpServer(const std::string& host, uint16_t port) override;
    void RunHttpLoop() override;
    void StopHttpServer() override;

    // IMqttLifecycle
    bool IsMqttRegistered() override;
    bool IsMqttEnabled() override;
    void MqttStop() override;
    void MqttStart() override;

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
    std::unique_ptr<HttpLifecycleServiceImpl> http_lifecycle_;
    std::unique_ptr<MqttLifecycleServiceImpl> mqtt_lifecycle_;
    std::unique_ptr<NetworkConfigServiceImpl> network_config_;
};

}  // namespace cosmo::service
