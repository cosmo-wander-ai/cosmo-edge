#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace cosmo::service::gb {
// Managed UDP RTP ingress -> loopback RFC4571. Retains the existing SRS session,
// PS recovery, codec and remux pipeline. Never accepts unallocated SSRCs.
// Control methods run on the management worker; packet I/O owns a separate thread.
class UdpMediaBridge {
public:
    explicit UdpMediaBridge(int mediaPort);
    ~UdpMediaBridge();
    UdpMediaBridge(const UdpMediaBridge&)            = delete;
    UdpMediaBridge& operator=(const UdpMediaBridge&) = delete;
    void Add(uint32_t ssrc, const std::string& sourceIp);
    void Remove(uint32_t ssrc);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cosmo::service::gb
