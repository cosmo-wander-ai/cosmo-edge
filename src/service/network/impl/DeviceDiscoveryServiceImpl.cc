// DeviceDiscoveryServiceImpl.cc — Core network lifecycle for multicast discovery.
// Command handlers → DiscoveryCommandHandler.cc

#include "service/network/impl/DeviceDiscoveryServiceImpl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <nlohmann/json.hpp>

#include "service/detail/ServiceRegistry.h"
#include "service/network/INetworkConfig.h"
#include "service/network/impl/DeviceDiscoveryReceivePolicy.h"
#include "util/ErrorCode.h"
#include "util/RuntimeIsolation.h"
#include "util/TimingConstants.h"

namespace cosmo::service {

namespace {

    constexpr auto kInitRetryInterval     = std::chrono::seconds(10);
    constexpr auto kRecoveryRetryInterval = std::chrono::seconds(1);

    void JoinThread(std::thread thread) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    bool WaitForStop(const std::atomic<bool>& stop, std::chrono::seconds duration) {
        for (auto elapsed = std::chrono::seconds::zero(); elapsed < duration;
             elapsed += cosmo::timing::kOneSecondInterval) {
            if (stop.load(std::memory_order_acquire)) {
                return true;
            }
            std::this_thread::sleep_for(cosmo::timing::kOneSecondInterval);
        }
        return stop.load(std::memory_order_acquire);
    }

}  // namespace

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
    : multicast_ip_(ip), multicast_port_(port), probe_queue_("discovery probe") {
    probe_queue_.SetProcessor([this](DiscoveryProbeRecv&& data) { HandleProbe(std::move(data)); });
}

DeviceDiscoveryServiceImpl::~DeviceDiscoveryServiceImpl() {
    DeviceDiscoveryServiceImpl::Stop();
}

void DeviceDiscoveryServiceImpl::Start() {
    if (runtime::IsIsolatedRuntime()) {
        return;
    }
    std::lock_guard<std::mutex> lock(thread_mtx_);
    if (stop_.load(std::memory_order_acquire) || init_thread_.joinable()) {
        return;
    }

    // Async init: retry in background thread to avoid blocking main process startup.
    init_thread_ = std::thread([this]() { InitLoop(); });
}

void DeviceDiscoveryServiceImpl::Stop() {
    stop_.store(true, std::memory_order_release);

    probe_queue_.Stop();
    probe_queue_.stop();

    std::thread init_thread;
    {
        std::lock_guard<std::mutex> lock(thread_mtx_);
        init_thread.swap(init_thread_);
    }

    JoinThread(std::move(init_thread));
    CloseSocket();
}

void DeviceDiscoveryServiceImpl::InitLoop() {
    while (!stop_.load(std::memory_order_acquire)) {
        if (0 == InitMulticast()) {
            LOG_INFO("{}", "device discovery service started successfully");
            auto receive_exit = RecvLoop();
            CloseSocket();
            if (receive_exit == ReceiveLoopExit::kStopped) {
                return;
            }
            LOG_WARN("{}", "device discovery receive socket failed, restarting in 1s");
            if (WaitForStop(stop_, kRecoveryRetryInterval)) {
                return;
            }
            continue;
        }
        LOG_WARN("{}", "init device discovery multicast failed, retrying in 10s");
        if (WaitForStop(stop_, kInitRetryInterval)) {
            return;
        }
    }
}

int DeviceDiscoveryServiceImpl::InitMulticast() {
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (inited_.load(std::memory_order_acquire)) {
        LOG_WARN("socket {} multicast has already been initialized", socket_fd_);
        return static_cast<int>(DiscoveryError::Multicast_Inited);
    }

    // Close a socket left by a previous partial initialization.
    CloseSocketLocked();

    auto fail = [this](DiscoveryError error) {
        CloseSocketLocked();
        return static_cast<int>(error);
    };

    // Create UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == socket_fd_) {
        LOG_WARN("socket failed, errno {}, errorMsg {}", errno, strerror(errno));
        return static_cast<int>(DiscoveryError::Socket_Failed);
    }
    LOG_INFO("socket {} create success", socket_fd_);

    // Allow port reuse
    unsigned int reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_WARN("setsockopt SO_REUSEADDR failed, errno {}, errorMsg {}", errno, strerror(errno));
        return fail(DiscoveryError::Socket_SetoptFailed);
    }

    // Disable multicast loopback
    unsigned char loop = 0;
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        LOG_WARN("setsockopt IP_MULTICAST_LOOP failed, errno {}, errorMsg {}", errno, strerror(errno));
        return fail(DiscoveryError::Socket_SetoptFailed);
    }

    // Set multicast TTL
    unsigned char ttl = 32;
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        LOG_WARN("setsockopt IP_MULTICAST_TTL failed, errno {}, errorMsg {}", errno, strerror(errno));
        return fail(DiscoveryError::Socket_SetoptFailed);
    }

    struct timeval recv_timeout {
        1, 0
    };
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0) {
        LOG_WARN("setsockopt SO_RCVTIMEO failed, errno {}, errorMsg {}", errno, strerror(errno));
        return fail(DiscoveryError::Socket_SetoptFailed);
    }

    // Set receive address
    struct sockaddr_in recvAddr;
    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family      = AF_INET;
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    recvAddr.sin_port        = htons(multicast_port_);
    if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&recvAddr), sizeof(recvAddr)) < 0) {
        LOG_WARN("bind failed, errno {}, errorMsg {}", errno, strerror(errno));
        return fail(DiscoveryError::Socket_BindFailed);
    }

    // Join multicast group
    if (false == JoinMulticastGroup()) {
        return fail(DiscoveryError::Socket_SetoptFailed);
    }

    inited_.store(true, std::memory_order_release);
    LOG_INFO("{}", "init multicast success ");
    return static_cast<int>(DiscoveryError::Success);
}

bool DeviceDiscoveryServiceImpl::JoinMulticastGroup() {
    struct in_addr group_address;
    if (inet_pton(AF_INET, multicast_ip_.c_str(), &group_address) != 1) {
        LOG_WARN("invalid multicast address {}", multicast_ip_);
        return false;
    }

    if (AddMulticastMembership(group_address.s_addr, htonl(INADDR_ANY))) {
        return true;
    }

    const int default_interface_error = errno;
    LOG_WARN("IP_ADD_MEMBERSHIP with default interface failed, errno {}, errorMsg {}; trying main interface",
             default_interface_error, strerror(default_interface_error));

    auto net_card_list = ServiceRegistry::Instance().Get<INetworkConfig>().GetCardRealInfos();
    for (const auto& net_card : net_card_list) {
        if (!net_card.is_main) {
            continue;
        }

        struct in_addr interface_address;
        if (inet_pton(AF_INET, net_card.ip_addr.c_str(), &interface_address) != 1) {
            LOG_WARN("invalid main interface address {}", net_card.ip_addr);
            return false;
        }

        LOG_INFO("IP_ADD_MEMBERSHIP with main interface {}", net_card.ip_addr);
        if (!AddMulticastMembership(group_address.s_addr, interface_address.s_addr)) {
            const int interface_error = errno;
            LOG_WARN("IP_ADD_MEMBERSHIP with main interface failed, errno {}, errorMsg {}", interface_error,
                     strerror(interface_error));
            return false;
        }
        return true;
    }

    LOG_WARN("{}", "cannot find main network interface for multicast membership");
    return false;
}

bool DeviceDiscoveryServiceImpl::AddMulticastMembership(uint32_t group_address, uint32_t interface_address) {
    struct ip_mreq request;
    request.imr_multiaddr.s_addr = group_address;
    request.imr_interface.s_addr = interface_address;
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) < 0) {
        return false;
    }

    multicast_group_address_     = group_address;
    multicast_interface_address_ = interface_address;
    multicast_joined_            = true;
    return true;
}

void DeviceDiscoveryServiceImpl::CloseSocket() {
    std::lock_guard<std::mutex> lock(socket_mtx_);
    CloseSocketLocked();
}

void DeviceDiscoveryServiceImpl::CloseSocketLocked() {
    if (socket_fd_ >= 0) {
        LOG_INFO("closing multicast socket fd {}", socket_fd_);
        if (multicast_joined_) {
            struct ip_mreq request;
            request.imr_multiaddr.s_addr = multicast_group_address_;
            request.imr_interface.s_addr = multicast_interface_address_;

            if (setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &request, sizeof(request)) < 0) {
                LOG_WARN("setsockopt {} IP_DROP_MEMBERSHIP failed, errno {}, errorMsg {}", socket_fd_, errno,
                         strerror(errno));
            }
        }
        if (close(socket_fd_) < 0) {
            LOG_WARN("close {} failed, errno {}, errorMsg {}", socket_fd_, errno, strerror(errno));
        }
    }

    socket_fd_                   = -1;
    multicast_group_address_     = 0;
    multicast_interface_address_ = 0;
    multicast_joined_            = false;
    inited_.store(false, std::memory_order_release);
}

DeviceDiscoveryServiceImpl::ReceiveLoopExit DeviceDiscoveryServiceImpl::RecvLoop() {
    LOG_INFO("{}", "receive msg thread create success!");

    int receive_fd = -1;
    {
        std::lock_guard<std::mutex> lock(socket_mtx_);
        receive_fd = socket_fd_;
    }
    if (receive_fd < 0) {
        LOG_WARN("{}", "multicast receive socket is not initialized");
        return ReceiveLoopExit::kRestartSocket;
    }

    struct sockaddr_in peeraddr;
    memset(&peeraddr, 0, sizeof(peeraddr));
    peeraddr.sin_family      = AF_INET;
    peeraddr.sin_addr.s_addr = inet_addr(multicast_ip_.c_str());
    peeraddr.sin_port        = htons(multicast_port_);

    // Receive multicast data
    std::array<char, 4096> message_buffer{};
    while (!stop_.load(std::memory_order_acquire)) {
        message_buffer.fill('\0');
        socklen_t address_length = sizeof(peeraddr);
        auto bytes               = recvfrom(receive_fd, message_buffer.data(), message_buffer.size() - 1, 0,
                                            reinterpret_cast<struct sockaddr*>(&peeraddr), &address_length);
        if (bytes < 0) {
            if (stop_.load(std::memory_order_acquire)) {
                return ReceiveLoopExit::kStopped;
            }

            const int receive_error = errno;
            if (detail::ClassifyMulticastReceiveError(receive_error) ==
                detail::MulticastReceiveAction::kRetry) {
                continue;
            }

            LOG_WARN("recvfrom multicast failed, errno {}, errorMsg {}; restarting socket", receive_error,
                     strerror(receive_error));
            return ReceiveLoopExit::kRestartSocket;
        }
        if (bytes == 0) {
            continue;
        }

        std::string message(message_buffer.data(), static_cast<size_t>(bytes));

        DiscoveryVagueMsg vagueMsg;
        try {
            {
                auto _j = nlohmann::json::parse(message);
                _j.get_to(vagueMsg);
            };
        } catch (const std::exception& e) {
            LOG_WARN("loadjson error, msg {}, len: {}, catch {}", message, bytes, e.what());
            continue;
        }

        LOG_INFO("receive multicast msg cmd: {}, type: {}, reqId: {} From:{}", vagueMsg.cmd, vagueMsg.type,
                 vagueMsg.reqId, platform::GetIPString(peeraddr.sin_addr.s_addr));

        if ("req" == vagueMsg.type && detail::IsProductionDiscoveryCommandAllowed(vagueMsg.cmd)) {
            LOG_INFO("receive multicast probe detail: {}, len: {}", message, bytes);
            vagueMsg.from = platform::GetIPString(peeraddr.sin_addr.s_addr);
            probe_queue_.Insert(vagueMsg);
        } else if ("req" == vagueMsg.type &&
                   detail::IsProductionDiscoveryCommandAuthenticationRequired(vagueMsg.cmd)) {
            LOG_WARN("Reject unauthenticated production discovery command: {}", vagueMsg.cmd);
            nlohmann::json response{{"cmd", vagueMsg.cmd},
                                    {"type", "ack"},
                                    {"reqId", vagueMsg.reqId},
                                    {"resCode", static_cast<int>(util::ErrorEnum::AuthFailed)},
                                    {"resMsg", "authentication_required"}};
            SendMessage(response.dump(), platform::GetIPString(peeraddr.sin_addr.s_addr));
        } else if ("req" == vagueMsg.type) {
            LOG_WARN("Ignore unknown production discovery command: {}", vagueMsg.cmd);
        }
    }
    return ReceiveLoopExit::kStopped;
}

void DeviceDiscoveryServiceImpl::SendMessage(std::string&& msg, const std::string& from) {
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (socket_fd_ < 0) {
        LOG_WARN("{}", "cannot send multicast response: socket is not initialized");
        return;
    }

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
