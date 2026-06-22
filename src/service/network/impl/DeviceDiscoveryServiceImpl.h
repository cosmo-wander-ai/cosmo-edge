// Device discovery service implementation — multicast-based device search.
// Migrated from tools/SearchMulticast into the service layer.
#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "service/network/DeviceDiscoveryTypes.h"
#include "service/network/IDeviceDiscoveryService.h"
#include "util/AsyncQueue.h"

namespace cosmo::service {

class DeviceDiscoveryServiceImpl : public IDeviceDiscoveryService {
public:
    DeviceDiscoveryServiceImpl(const DeviceDiscoveryServiceImpl&)            = delete;
    DeviceDiscoveryServiceImpl& operator=(const DeviceDiscoveryServiceImpl&) = delete;

    explicit DeviceDiscoveryServiceImpl(const std::string& multicast_ip = "239.255.0.0",
                                        int multicast_port              = 46000);
    ~DeviceDiscoveryServiceImpl() override;

    void Start() override;
    void Stop() override;

private:
    // Internal message envelope for async processing.
    struct InternalMsg {
        std::string from;
        std::string raw_json;
        DiscoveryVagueMsg vague;
    };

    // Multicast initialization (returns 0 on success).
    int InitMulticast();
    bool JoinMulticastGroup();
    void CloseSocket();

    // Background loops.
    void InitLoop();  // Async init with retry.
    void RecvLoop();  // Receive multicast messages.

    // Message sending.
    void SendMessage(std::string&& msg, const std::string& peer_ip);

    // Message handlers.
    void HandleProbe(DiscoveryProbeRecv&& data);
    void HandleRecvMsg(InternalMsg&& data);
    void HandleModifyNetCard(InternalMsg&& data);
    void HandleWriteHWInfo(InternalMsg&& data);
    void HandleModifyAuthCode(InternalMsg&& data);
    void HandleQueryAuthMessage(InternalMsg&& data);

    // Utilities.
    bool WriteHWInfo(const DeviceHWInfo& info);
    bool GetToken(std::string& token, std::error_condition& errc, std::string& result);

    std::atomic<bool> inited_{false};
    std::atomic<bool> stop_{false};
    std::string multicast_ip_;
    int multicast_port_;
    int socket_fd_{0};
    std::thread init_thread_;
    std::thread recv_thread_;
    std::thread netcard_thread_;
    AsyncQueue<DiscoveryProbeRecv> probe_queue_;
    AsyncQueue<InternalMsg> async_queue_;
};

}  // namespace cosmo::service
