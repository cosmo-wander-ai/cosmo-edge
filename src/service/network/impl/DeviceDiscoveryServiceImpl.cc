// DeviceDiscoveryServiceImpl.cc — Core network lifecycle for multicast discovery.
// Command handlers → DiscoveryCommandHandler.cc

#include "service/network/impl/DeviceDiscoveryServiceImpl.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <nlohmann/json.hpp>

#include "platform/NetCardOp.h"
#include "platform/WifiPlatform.h"
#include "service/network/impl/DeviceDiscoveryReceivePolicy.h"
#include "util/ErrorCode.h"
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

    if (setsockopt(socket_fd_, IPPROTO_IP, IP_PKTINFO, &reuse, sizeof(reuse)) < 0) {
        LOG_WARN("setsockopt IP_PKTINFO failed, errno {}", errno);
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
    in_addr group_address{};
    if (inet_pton(AF_INET, multicast_ip_.c_str(), &group_address) != 1 ||
        !IN_MULTICAST(ntohl(group_address.s_addr))) {
        LOG_WARN("invalid multicast address {}", multicast_ip_);
        return false;
    }
    multicast_group_address_ = group_address.s_addr;
    return UpdateMulticastMemberships();
}

bool DeviceDiscoveryServiceImpl::UpdateMulticastMemberships() {
    std::map<unsigned, uint32_t> desired;
    for (const auto& [name, address] :
         detail::DiscoveryMembershipAddresses(platform::ReadNetworkInterfaces())) {
        const auto index = if_nametoindex(name.c_str());
        in_addr interface_address{};
        if (index != 0 && inet_pton(AF_INET, address.c_str(), &interface_address) == 1) {
            desired[index] = interface_address.s_addr;
        }
    }
    for (auto it = multicast_memberships_.begin(); it != multicast_memberships_.end();) {
        const auto desired_it = desired.find(it->first);
        if (desired_it != desired.end() && desired_it->second == it->second) {
            ++it;
            continue;
        }
        ip_mreqn request{};
        request.imr_multiaddr.s_addr = multicast_group_address_;
        request.imr_address.s_addr   = it->second;
        request.imr_ifindex          = static_cast<int>(it->first);
        // A removed interface may already have lost its kernel membership.
        setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &request, sizeof(request));
        it = multicast_memberships_.erase(it);
    }
    for (const auto& [index, address] : desired) {
        if (multicast_memberships_.count(index) != 0) {
            continue;
        }
        ip_mreqn request{};
        request.imr_multiaddr.s_addr = multicast_group_address_;
        request.imr_address.s_addr   = address;
        request.imr_ifindex          = static_cast<int>(index);
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) < 0) {
            LOG_WARN("IP_ADD_MEMBERSHIP failed on interface {}, errno {}", index, errno);
            continue;
        }
        multicast_memberships_[index] = address;
    }
    // Stay alive without an address so link/address recovery can add memberships.
    return true;
}

void DeviceDiscoveryServiceImpl::CloseSocket() {
    std::lock_guard<std::mutex> lock(socket_mtx_);
    CloseSocketLocked();
}

void DeviceDiscoveryServiceImpl::CloseSocketLocked() {
    if (socket_fd_ >= 0) {
        LOG_INFO("closing multicast socket fd {}", socket_fd_);
        for (const auto& [index, address] : multicast_memberships_) {
            ip_mreqn request{};
            request.imr_multiaddr.s_addr = multicast_group_address_;
            request.imr_address.s_addr   = address;
            request.imr_ifindex          = static_cast<int>(index);
            setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &request, sizeof(request));
        }
        if (close(socket_fd_) < 0) {
            LOG_WARN("close {} failed, errno {}, errorMsg {}", socket_fd_, errno, strerror(errno));
        }
    }

    socket_fd_               = -1;
    multicast_group_address_ = 0;
    multicast_memberships_.clear();
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

    std::array<char, 4096> message_buffer{};
    auto last_membership_check = std::chrono::steady_clock::now();
    while (!stop_.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_membership_check >= timing::kOneSecondInterval) {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            UpdateMulticastMemberships();
            last_membership_check = now;
        }
        sockaddr_in peeraddr{};
        alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(in_pktinfo))> control{};
        iovec payload{message_buffer.data(), message_buffer.size()};
        msghdr received{};
        received.msg_name       = &peeraddr;
        received.msg_namelen    = sizeof(peeraddr);
        received.msg_iov        = &payload;
        received.msg_iovlen     = 1;
        received.msg_control    = control.data();
        received.msg_controllen = control.size();
        const auto bytes        = recvmsg(receive_fd, &received, 0);
        if (bytes < 0) {
            if (stop_.load(std::memory_order_acquire)) {
                return ReceiveLoopExit::kStopped;
            }

            const int receive_error = errno;
            if (detail::ClassifyMulticastReceiveError(receive_error) ==
                detail::MulticastReceiveAction::kRetry) {
                continue;
            }

            LOG_WARN("recvmsg multicast failed, errno {}, errorMsg {}; restarting socket", receive_error,
                     strerror(receive_error));
            return ReceiveLoopExit::kRestartSocket;
        }
        if (bytes == 0 || (received.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
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

        for (auto* header = CMSG_FIRSTHDR(&received); header != nullptr;
             header       = CMSG_NXTHDR(&received, header)) {
            if (header->cmsg_level == IPPROTO_IP && header->cmsg_type == IP_PKTINFO &&
                header->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo))) {
                in_pktinfo packet_info{};
                std::memcpy(&packet_info, CMSG_DATA(header), sizeof(packet_info));
                vagueMsg.receive_interface_index = static_cast<unsigned>(packet_info.ipi_ifindex);
                vagueMsg.receive_local_address   = packet_info.ipi_spec_dst.s_addr;
            }
        }
        if (vagueMsg.receive_interface_index == 0 || vagueMsg.receive_local_address == 0) {
            LOG_WARN("{}", "ignore discovery packet without receiving interface metadata");
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
            SendMessage(response.dump(), platform::GetIPString(peeraddr.sin_addr.s_addr),
                        vagueMsg.receive_interface_index, vagueMsg.receive_local_address);
        } else if ("req" == vagueMsg.type) {
            LOG_WARN("Ignore unknown production discovery command: {}", vagueMsg.cmd);
        }
    }
    return ReceiveLoopExit::kStopped;
}

void DeviceDiscoveryServiceImpl::SendMessage(std::string&& msg, const std::string& from,
                                             unsigned interface_index, uint32_t local_address) {
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (socket_fd_ < 0 || interface_index == 0 || local_address == 0) {
        LOG_WARN("{}", "cannot send discovery response without socket and receiving interface");
        return;
    }

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port   = htons(multicast_port_);
    inet_pton(AF_INET, multicast_ip_.c_str(), &destination.sin_addr);
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(in_pktinfo))> control{};
    iovec payload{msg.data(), msg.size()};
    msghdr outgoing{};
    outgoing.msg_name       = &destination;
    outgoing.msg_namelen    = sizeof(destination);
    outgoing.msg_iov        = &payload;
    outgoing.msg_iovlen     = 1;
    outgoing.msg_control    = control.data();
    outgoing.msg_controllen = control.size();
    auto* header            = CMSG_FIRSTHDR(&outgoing);
    header->cmsg_level      = IPPROTO_IP;
    header->cmsg_type       = IP_PKTINFO;
    header->cmsg_len        = CMSG_LEN(sizeof(in_pktinfo));
    in_pktinfo packet_info{};
    packet_info.ipi_ifindex         = static_cast<int>(interface_index);
    packet_info.ipi_spec_dst.s_addr = local_address;
    std::memcpy(CMSG_DATA(header), &packet_info, sizeof(packet_info));

    auto ret = sendmsg(socket_fd_, &outgoing, 0);
    if (ret < 0 && inet_pton(AF_INET, from.c_str(), &destination.sin_addr) == 1) {
        ret = sendmsg(socket_fd_, &outgoing, 0);
    }
    LOG_INFO("send discovery ack: {}, return: {}", ret < 0 ? "failed" : "success", ret);
}

}  // namespace cosmo::service
