#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cosmo::service::gb {
// Single-source PS/RTP, bounded 50 ms / 64 packet reorder window. Missing
// sequence numbers remain missing so the downstream PS parser detects loss.
class RtpReorder {
public:
    using Time = std::chrono::steady_clock::time_point;
    static bool Header(const std::string& packet, uint32_t& ssrc, uint16_t& sequence);
    bool Push(std::string packet, Time now);
    std::vector<std::string> Pop(Time now);

private:
    std::map<uint16_t, std::string> packets_;
    uint16_t next_{0};
    bool started_{false};
    size_t bytes_{0};
    Time gap_{};
};
}  // namespace cosmo::service::gb
