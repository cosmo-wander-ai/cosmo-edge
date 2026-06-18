// DeviceDiscoveryServiceImpl.cc — Core network lifecycle for multicast discovery.
// Command handlers → DiscoveryCommandHandler.cc

#include "service/network/impl/DeviceDiscoveryServiceImpl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <nlohmann/json.hpp>

#include "service/detail/ServiceRegistry.h"
#include "service/network/INetworkService.h"
#include "service/system/IDeviceInfoService.h"
#include "util/LimitedTypeJson.h"
#include "util/TimingConstants.h"

namespace cosmo::service {

// Internal error codes for multicast init.
enum class DiscoveryError {
    Success = 0,
    Multicast_UnInited,
    Multicast_Inited,
    Socket_Failed,
    Socket_SetoptFailed,
    Socket_BindFailed
};

DeviceDiscoveryServiceImpl::DeviceDiscoveryServiceImpl(const std::string& ip, int port)
    : multicast_ip_(ip),
      multicast_port_(port),
      probe_queue_("discovery probe"),
      async_queue_("discovery async") {
    probe_queue_.SetProcessor([this](DiscoveryProbeRecv&& data) { HandleProbe(std::move(data)); });
    async_queue_.SetProcessor([this](InternalMsg&& data) { HandleRecvMsg(std::move(data)); });
}

DeviceDiscoveryServiceImpl::~DeviceDiscoveryServiceImpl() {
    DeviceDiscoveryServiceImpl::Stop();
}

void DeviceDiscoveryServiceImpl::Start() {
    stop_.store(false, std::memory_order_release);
    // Async init: retry in background thread to avoid blocking main process startup.
    init_thread_ = std::thread([this]() { InitLoop(); });
}

void DeviceDiscoveryServiceImpl::Stop() {
    stop_.store(true, std::memory_order_release);
    CloseSocket();
    if (init_thread_.joinable()) {
        init_thread_.join();
    }
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    if (netcard_thread_.joinable()) {
        netcard_thread_.join();
    }
}

void DeviceDiscoveryServiceImpl::InitLoop() {
    constexpr auto k_retry_interval = std::chrono::seconds(10);
    while (!stop_.load(std::memory_order_acquire)) {
        if (0 == InitMulticast()) {
            // Init succeeded, start receive loop.
            if (!recv_thread_.joinable()) {
                recv_thread_ = std::thread([this]() { RecvLoop(); });
            }
            LOG_INFO("{}", "device discovery service started successfully");
            return;
        }
        LOG_ERRO("{}", "init device discovery multicast failed, retrying in 10s");
        // Sleep with periodic stop check.
        for (int i = 0; i < 10 && !stop_.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(cosmo::timing::kOneSecondInterval);
        }
    }
}

int DeviceDiscoveryServiceImpl::InitMulticast() {
    if (inited_.load(std::memory_order_acquire)) {
        LOG_ERRO("socket {} multicast has been initialized", socket_fd_);
        return static_cast<int>(DiscoveryError::Multicast_Inited);
    }

    // Close if previously initialized but not fully successful
    CloseSocket();

    // Create UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == socket_fd_) {
        LOG_ERRO("socket failed, errno {}, errorMsg {}", errno, strerror(errno));
        return static_cast<int>(DiscoveryError::Socket_Failed);
    }
    LOG_INFO("socket {} create success", socket_fd_);

    // Allow port reuse
    unsigned int reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_ERRO("setsockopt SO_REUSEADDR failed, errno {}, errorMsg {}", errno, strerror(errno));
        return static_cast<int>(DiscoveryError::Socket_SetoptFailed);
    }

    // Disable multicast loopback
    unsigned char loop = 0;
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        LOG_ERRO("setsockopt IP_MULTICAST_LOOP failed, errno {}, errorMsg {}", errno, strerror(errno));
        return static_cast<int>(DiscoveryError::Socket_SetoptFailed);
    }

    // Set multicast TTL
    unsigned char ttl = 32;
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        LOG_ERRO("setsockopt IP_MULTICAST_TTL failed, errno {}, errorMsg {}", errno, strerror(errno));
        return static_cast<int>(DiscoveryError::Socket_SetoptFailed);
    }

    // Set receive address
    struct sockaddr_in recvAddr;
    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family      = AF_INET;
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    recvAddr.sin_port        = htons(multicast_port_);
    if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&recvAddr), sizeof(recvAddr)) < 0) {
        LOG_ERRO("bind failed, errno {}, errorMsg {}", errno, strerror(errno));
        return static_cast<int>(DiscoveryError::Socket_BindFailed);
    }

    // Join multicast group
    if (false == JoinMulticastGroup()) {
        return static_cast<int>(DiscoveryError::Socket_SetoptFailed);
    }

    inited_.store(true, std::memory_order_release);
    LOG_INFO("{}", "init multicast success ");
    return static_cast<int>(DiscoveryError::Success);
}

bool DeviceDiscoveryServiceImpl::JoinMulticastGroup() {
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(multicast_ip_.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        LOG_WARN("setsockopt IP_ADD_MEMBERSHIP failed, errno {}, errorMsg {} Need Local IP", errno,
                 strerror(errno));

        auto net_card_list = ServiceRegistry::Instance().Get<INetworkService>().GetCardRealInfos();
        if (net_card_list.empty()) {
            LOG_ERRO("{}", "get net_card_list failed");
            return false;
        }
        bool b_find_ip = false;
        std::string sub_card_ip;
        for (auto net_card : net_card_list) {
            if (net_card.is_main) {
                mreq.imr_interface.s_addr = inet_addr(net_card.ip_addr.c_str());
                b_find_ip                 = true;
                LOG_INFO("setsockopt IP_ADD_MEMBERSHIP with Local Ip {}", net_card.ip_addr);
                break;
            } else {
                // use sub card ip for binding
                sub_card_ip = net_card.ip_addr;
            }
        }
        if (false == b_find_ip) {
            LOG_ERRO("setsockopt IP_ADD_MEMBERSHIP But Can't Get MainCard IP, To Do SubCard Ip:{}",
                     sub_card_ip);
            return false;
        }
        // 2021-11-16 Setting INADDR_ANY in DHCP mode causes errors
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            LOG_WARN("setsockopt IP_ADD_MEMBERSHIP with Local Ip failed, errno {}, errorMsg {}", errno,
                     strerror(errno));
            return false;
        }
    }
    return true;
}

void DeviceDiscoveryServiceImpl::CloseSocket() {
    if (0 != socket_fd_) {
        LOG_INFO("socket fd {} should be closed", socket_fd_);
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(multicast_ip_.c_str());
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);

        // Leave multicast group
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, reinterpret_cast<char*>(&mreq),
                       sizeof(mreq)) < 0) {
            LOG_ERRO("setsockopt {} IP_DROP_MEMBERSHIP failed, errno {}, errorMsg {}", socket_fd_, errno,
                     strerror(errno));
        }
        // Close socket
        // if (close(socket_fd_))
        if (shutdown(socket_fd_, SHUT_RDWR) < 0)  // Wake up blocking recvfrom
        {
            LOG_ERRO("shutdown {} failed, errno {}, errorMsg {}", socket_fd_, errno, strerror(errno));
        }
        socket_fd_ = 0;
    } else {
        LOG_INFO("{}", "socket uninitialized");
    }
}

void DeviceDiscoveryServiceImpl::RecvLoop() {
    LOG_INFO("{}", "receive msg thread create success!");

    struct sockaddr_in peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    peeraddr.sin_family      = AF_INET;
    peeraddr.sin_addr.s_addr = inet_addr(multicast_ip_.c_str());
    peeraddr.sin_port        = htons(multicast_port_);
    int addrlen              = sizeof(sockaddr_in);

    // Receive multicast data
    char msgBuf[4096];
    while (!stop_.load(std::memory_order_acquire)) {
        memset(msgBuf, 0, sizeof(msgBuf));
        auto bytes =
            recvfrom(socket_fd_, msgBuf, sizeof(msgBuf), 0, reinterpret_cast<struct sockaddr*>(&peeraddr),
                     reinterpret_cast<socklen_t*>(&addrlen));
        if (bytes <= 0) {
            LOG_ERRO("recvfrom mutlicast errno {}, errorMsg {}", errno, strerror(errno));
            // Prevent high CPU usage from continuous errors
            std::this_thread::sleep_for(cosmo::timing::kOneSecondInterval);
            // Receive error — possibly due to local IP address change
            JoinMulticastGroup();
            continue;
        }

        DiscoveryVagueMsg vagueMsg;
        try {
            {
                auto _j = nlohmann::json::parse(msgBuf);
                _j.get_to(vagueMsg);
            };
        } catch (const std::exception& e) {
            LOG_ERRO("loadjson error, msg {}, len: {}, catch {}", msgBuf, bytes, e.what());
            continue;
        }

        LOG_INFO("receive multicast msg cmd: {}, type: {}, reqId: {} From:{}", vagueMsg.cmd, vagueMsg.type,
                 vagueMsg.reqId, platform::GetIPString(peeraddr.sin_addr.s_addr));

        // Only process "req" request messages; for probe, ensure SN matches this device
        if ("req" == vagueMsg.type) {
            LOG_INFO("receive multicast msg detail: {}, len: {}", msgBuf, bytes);

            if ("probe" == vagueMsg.cmd) {
                vagueMsg.from = platform::GetIPString(peeraddr.sin_addr.s_addr);
                probe_queue_.Insert(vagueMsg);
            } else if ("writeHWInfo" == vagueMsg.cmd) {
                InternalMsg data;
                data.vague    = vagueMsg;
                data.raw_json = msgBuf;
                data.from     = platform::GetIPString(peeraddr.sin_addr.s_addr);
                async_queue_.Insert(data);
            } else if ("modifyAuthCode" == vagueMsg.cmd) {
                InternalMsg data;
                data.vague    = vagueMsg;
                data.raw_json = msgBuf;
                data.from     = platform::GetIPString(peeraddr.sin_addr.s_addr);
                async_queue_.Insert(data);
            } else if ("queryAuthMessage" == vagueMsg.cmd) {
                InternalMsg data;
                data.vague    = vagueMsg;
                data.raw_json = msgBuf;
                data.from     = platform::GetIPString(peeraddr.sin_addr.s_addr);
                async_queue_.Insert(data);
            } else if (ServiceRegistry::Instance().Get<IDeviceInfoService>().GetDevSn() ==
                       vagueMsg.deviceSn) {
                if ("modifyNetCard" == vagueMsg.cmd) {
                    InternalMsg data;
                    data.vague    = vagueMsg;
                    data.raw_json = msgBuf;
                    data.from     = platform::GetIPString(peeraddr.sin_addr.s_addr);
                    async_queue_.Insert(data);
                }
            }
        }
    }
}

void DeviceDiscoveryServiceImpl::SendMessage(std::string&& msg, const std::string& from) {
    struct sockaddr_in sendAddr;
    memset(&sendAddr, 0, sizeof(sendAddr));
    sendAddr.sin_family      = AF_INET;
    sendAddr.sin_addr.s_addr = inet_addr(multicast_ip_.c_str());
    sendAddr.sin_port        = htons(multicast_port_);
    auto ret = sendto(socket_fd_, msg.c_str(), msg.size(), 0, reinterpret_cast<const sockaddr*>(&sendAddr),
                      sizeof(sendAddr));

    if ((ret < 0) && (from.size() > 0)) {
        LOG_WARN("{}", "send ack to multicastIp Fail, Current is DHCP mode? Need send to Peer");
        memset(&sendAddr, 0, sizeof(sendAddr));
        sendAddr.sin_family      = AF_INET;
        sendAddr.sin_addr.s_addr = inet_addr(from.c_str());
        sendAddr.sin_port        = htons(multicast_port_);
        ret = sendto(socket_fd_, msg.c_str(), msg.size(), 0, reinterpret_cast<const sockaddr*>(&sendAddr),
                     sizeof(sendAddr));
    }
    LOG_INFO("send ack: {}, return: {}, sendMsg: {}", ret < 0 ? "failed" : "success", ret, msg);
}

}  // namespace cosmo::service
