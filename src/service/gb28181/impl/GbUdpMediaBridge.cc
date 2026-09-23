#include "service/gb28181/impl/GbUdpMediaBridge.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "service/gb28181/impl/GbRtpReorder.h"

namespace cosmo::service::gb {
struct UdpMediaBridge::Impl {
    using Clock = std::chrono::steady_clock;
    struct Lease {
        uint32_t source;
        std::atomic<bool> active{true};
    };
    struct Stream {
        std::shared_ptr<Lease> lease;
        int fd{-1};
        uint16_t sourcePort{0};
        bool connected{false}, failed{false};
        RtpReorder reorder;
        std::string output;
        Clock::time_point last{Clock::now()};
        ~Stream() {
            if (fd >= 0)
                close(fd);
        }
        void Fail() {
            failed = true;
            if (fd >= 0)
                close(fd);
            fd = -1;
            output.clear();
            reorder = {};
        }
    };
    int port, udp{-1};
    std::atomic<bool> running{false};
    std::thread worker;
    std::mutex leases_mtx_;
    std::map<uint32_t, std::shared_ptr<Lease>> leases;
    explicit Impl(int value) : port(value) {
        if (port < 1024 || port > 65535)
            throw std::runtime_error("media_unavailable");
        udp = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (udp < 0)
            throw std::runtime_error("media_unavailable");
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(port);
        address.sin_addr.s_addr = INADDR_ANY;
        if (bind(udp, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
            close(udp);
            udp = -1;
            throw std::runtime_error("udp_media_listen_failed");
        }
        int size = 4 * 1024 * 1024;
        setsockopt(udp, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
        running = true;
        try {
            worker = std::thread([this] { Run(); });
        } catch (...) {
            close(udp);
            udp = -1;
            throw;
        }
    }
    ~Impl() {
        running = false;
        if (worker.joinable())
            worker.join();
        if (udp >= 0)
            close(udp);
    }
    void Run() {
        std::map<uint32_t, std::unique_ptr<Stream>> streams;
        while (running.load()) {
            try {
                std::map<uint32_t, std::shared_ptr<Lease>> desired;
                {
                    std::lock_guard<std::mutex> lock(leases_mtx_);
                    desired = leases;
                }
                for (auto it = streams.begin(); it != streams.end();)
                    if (!desired.count(it->first) || desired.at(it->first) != it->second->lease)
                        it = streams.erase(it);
                    else
                        ++it;
                for (const auto& item : desired) {
                    if (streams.count(item.first))
                        continue;
                    auto stream       = std::make_unique<Stream>();
                    stream->lease     = item.second;
                    stream->fd        = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
                    const int noDelay = 1;
                    if (stream->fd >= 0)
                        setsockopt(stream->fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
                    sockaddr_in target{};
                    target.sin_family      = AF_INET;
                    target.sin_port        = htons(port);
                    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    if (stream->fd < 0 ||
                        (connect(stream->fd, reinterpret_cast<sockaddr*>(&target), sizeof(target)) < 0 &&
                         errno != EINPROGRESS))
                        stream->Fail();
                    streams.emplace(item.first, std::move(stream));
                }
                std::vector<pollfd> polls{{udp, POLLIN, 0}};
                std::vector<Stream*> writers;
                for (auto& item : streams) {
                    auto& stream = *item.second;
                    if (!stream.failed && (!stream.connected || !stream.output.empty())) {
                        polls.push_back({stream.fd, POLLOUT, 0});
                        writers.push_back(&stream);
                    }
                }
                if (poll(polls.data(), polls.size(), 10) < 0)
                    continue;
                const auto now = Clock::now();
                if (polls[0].revents & POLLIN) {
                    // Drain and forward smaller batches than the reorder window;
                    // a queued burst must not overflow it before Pop can run.
                    for (int count = 0; count < 32; ++count) {
                        char bytes[65536];
                        sockaddr_in peer{};
                        socklen_t length = sizeof(peer);
                        const auto size  = recvfrom(udp, bytes, sizeof(bytes), 0,
                                                    reinterpret_cast<sockaddr*>(&peer), &length);
                        if (size <= 0)
                            break;
                        std::string packet(bytes, size);
                        uint32_t ssrc;
                        uint16_t sequence;
                        if (!RtpReorder::Header(packet, ssrc, sequence) || !streams.count(ssrc))
                            continue;
                        auto& stream = *streams.at(ssrc);
                        if (stream.failed || !stream.lease->active.load() ||
                            stream.lease->source != peer.sin_addr.s_addr ||
                            (stream.sourcePort && stream.sourcePort != peer.sin_port))
                            continue;
                        if (stream.reorder.Push(std::move(packet), now)) {
                            stream.sourcePort = peer.sin_port;
                            stream.last       = now;
                        }
                    }
                }
                for (size_t index = 0; index < writers.size(); ++index) {
                    auto& stream = *writers[index];
                    if (!stream.lease->active.load()) {
                        stream.Fail();
                        continue;
                    }
                    const auto events = polls[index + 1].revents;
                    if (events & (POLLERR | POLLHUP | POLLNVAL)) {
                        stream.Fail();
                        continue;
                    }
                    if (!(events & POLLOUT))
                        continue;
                    if (!stream.connected) {
                        int error        = 0;
                        socklen_t length = sizeof(error);
                        if (getsockopt(stream.fd, SOL_SOCKET, SO_ERROR, &error, &length) || error) {
                            stream.Fail();
                            continue;
                        }
                        stream.connected = true;
                    }
                    if (!stream.output.empty()) {
                        const auto sent =
                            send(stream.fd, stream.output.data(), stream.output.size(), MSG_NOSIGNAL);
                        if (sent > 0)
                            stream.output.erase(0, sent);
                        else if (sent < 0 && errno != EAGAIN && errno != EINTR)
                            stream.Fail();
                    }
                }
                for (auto& item : streams) {
                    auto& stream = *item.second;
                    if (stream.failed)
                        continue;
                    if (!stream.lease->active.load() || now - stream.last > std::chrono::seconds(20)) {
                        stream.Fail();
                        continue;
                    }
                    for (const auto& packet : stream.reorder.Pop(now)) {
                        if (stream.output.size() + packet.size() + 2 > 256 * 1024) {
                            stream.Fail();
                            break;
                        }
                        stream.output += char(packet.size() >> 8);
                        stream.output += char(packet.size() & 255);
                        stream.output += packet;
                    }
                }
            } catch (...) {
                // Fail closed; management's existing media timeout releases leases
                // and retries with a fresh SSRC. Never terminate the process.
                for (auto& item : streams)
                    item.second->Fail();
            }
        }
    }
};
UdpMediaBridge::UdpMediaBridge(int mediaPort) : impl_(std::make_unique<Impl>(mediaPort)) {}
UdpMediaBridge::~UdpMediaBridge() = default;
void UdpMediaBridge::Add(uint32_t ssrc, const std::string& sourceIp) {
    auto lease = std::make_shared<Impl::Lease>();
    in_addr address{};
    if (!ssrc || inet_pton(AF_INET, sourceIp.c_str(), &address) != 1)
        throw std::runtime_error("invalid_parameter");
    lease->source = address.s_addr;
    std::lock_guard<std::mutex> lock(impl_->leases_mtx_);
    if (impl_->leases.size() >= 256 || impl_->leases.count(ssrc))
        throw std::runtime_error("busy");
    impl_->leases.emplace(ssrc, std::move(lease));
}
void UdpMediaBridge::Remove(uint32_t ssrc) {
    std::lock_guard<std::mutex> lock(impl_->leases_mtx_);
    auto it = impl_->leases.find(ssrc);
    if (it != impl_->leases.end()) {
        it->second->active = false;
        impl_->leases.erase(it);
    }
}
}  // namespace cosmo::service::gb
