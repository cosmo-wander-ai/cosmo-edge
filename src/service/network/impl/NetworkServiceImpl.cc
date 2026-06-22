// NetworkServiceImpl — Network Service Impl implementation.

#include "service/network/impl/NetworkServiceImpl.h"

#include "service/network/impl/HttpLifecycleServiceImpl.h"
#include "service/network/impl/MqttLifecycleServiceImpl.h"
#include "service/network/impl/NetworkConfigServiceImpl.h"

namespace cosmo::service {

NetworkServiceImpl::NetworkServiceImpl(DispatcherFactory http_dispatcher_factory,
                                       DispatcherFactory mqtt_dispatcher_factory)
    : http_lifecycle_(std::make_unique<HttpLifecycleServiceImpl>(std::move(http_dispatcher_factory))),
      mqtt_lifecycle_(std::make_unique<MqttLifecycleServiceImpl>(std::move(mqtt_dispatcher_factory))),
      network_config_(std::make_unique<NetworkConfigServiceImpl>()) {}

NetworkServiceImpl::~NetworkServiceImpl() = default;

// ---- HTTP Server lifecycle ----

void NetworkServiceImpl::InitHttpServer(const std::string& host, uint16_t port) {
    http_lifecycle_->InitHttpServer(host, port);
}

void NetworkServiceImpl::RunHttpLoop() {
    http_lifecycle_->RunHttpLoop();
}

void NetworkServiceImpl::StopHttpServer() {
    http_lifecycle_->StopHttpServer();
}

// ---- MQTT ----

bool NetworkServiceImpl::IsMqttRegistered() {
    return mqtt_lifecycle_->IsMqttRegistered();
}

bool NetworkServiceImpl::IsMqttEnabled() {
    return mqtt_lifecycle_->IsMqttEnabled();
}

void NetworkServiceImpl::MqttStop() {
    mqtt_lifecycle_->MqttStop();
}

void NetworkServiceImpl::MqttStart() {
    mqtt_lifecycle_->MqttStart();
}

// ---- NIC configuration ----

void NetworkServiceImpl::Init() {
    network_config_->Init();
}

platform::NetCardInfo NetworkServiceImpl::GetCardRealInfo(bool main) {
    return network_config_->GetCardRealInfo(main);
}

std::vector<platform::NetCardInfo> NetworkServiceImpl::GetCardRealInfos() {
    return network_config_->GetCardRealInfos();
}

bool NetworkServiceImpl::SetCardInfo(const platform::NetCardInfo& info) {
    return network_config_->SetCardInfo(info);
}

void NetworkServiceImpl::ApplyCardInfoAsync(const platform::NetCardInfo& info) {
    network_config_->ApplyCardInfoAsync(info);
}

std::vector<std::string> NetworkServiceImpl::GetCfgDns() {
    return network_config_->GetCfgDns();
}

bool NetworkServiceImpl::SetDnss(std::vector<std::string> dnss) {
    return network_config_->SetDnss(dnss);
}

bool NetworkServiceImpl::SearchSetNewInfo(platform::NetCardInfo& netCard, const std::string& dns1,
                                          const std::string& dns2) {
    return network_config_->SearchSetNewInfo(netCard, dns1, dns2);
}

// ---- Network Card Query ----

std::vector<NetCardView> NetworkServiceImpl::GetNetCards() {
    return network_config_->GetNetCards();
}

std::string NetworkServiceImpl::GetHostIpAddress() {
    return network_config_->GetHostIpAddress();
}

INetworkConfig::PingQualityResult NetworkServiceImpl::ProbeNetworkQuality(const std::string& ip,
                                                                          int packet_size) {
    return network_config_->ProbeNetworkQuality(ip, packet_size);
}

bool NetworkServiceImpl::IsIpAccessible(const std::string& ip) {
    return network_config_->IsIpAccessible(ip);
}

}  // namespace cosmo::service
