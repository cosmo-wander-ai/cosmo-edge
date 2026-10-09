#include "service/gb28181/impl/GbRtpReorder.h"

namespace cosmo::service::gb {
bool RtpReorder::Header(const std::string& packet, uint32_t& ssrc, uint16_t& sequence) {
    const auto* p   = reinterpret_cast<const unsigned char*>(packet.data());
    const auto size = packet.size();
    if (size < 12 || size > 65507 || p[0] >> 6 != 2 || (p[1] & 127) != 96)
        return false;
    size_t header = 12 + 4 * (p[0] & 15);
    if (header > size)
        return false;
    if (p[0] & 16) {
        if (header + 4 > size)
            return false;
        header += 4 + 4 * ((p[header + 2] << 8) | p[header + 3]);
    }
    const size_t padding = p[0] & 32 ? p[size - 1] : 0;
    if ((p[0] & 32 && padding == 0) || header + padding >= size)
        return false;
    sequence = (p[2] << 8) | p[3];
    ssrc     = (uint32_t(p[8]) << 24) | (uint32_t(p[9]) << 16) | (uint32_t(p[10]) << 8) | p[11];
    return ssrc != 0;
}
bool RtpReorder::Push(std::string packet, Time now) {
    uint32_t ssrc;
    uint16_t sequence;
    if (!Header(packet, ssrc, sequence))
        return false;
    if (!started_) {
        next_    = sequence;
        started_ = true;
    }
    if (uint16_t(sequence - next_) >= 32768 || packets_.count(sequence))
        return false;
    if (packets_.size() >= 64 || bytes_ + packet.size() > 128 * 1024)
        return false;
    bytes_ += packet.size();
    packets_.emplace(sequence, std::move(packet));
    if (gap_ == Time{})
        gap_ = now;
    return true;
}
std::vector<std::string> RtpReorder::Pop(Time now) {
    std::vector<std::string> result;
    while (!packets_.empty()) {
        auto it = packets_.find(next_);
        if (it == packets_.end()) {
            if (packets_.size() < 64 && now - gap_ < std::chrono::milliseconds(50))
                break;
            // Select by forward modular distance, not map order (sequence wraps).
            it = packets_.begin();
            for (auto candidate = packets_.begin(); candidate != packets_.end(); ++candidate)
                if (uint16_t(candidate->first - next_) < uint16_t(it->first - next_))
                    it = candidate;
            next_ = it->first;
        }
        bytes_ -= it->second.size();
        result.push_back(std::move(it->second));
        packets_.erase(it);
        ++next_;
        gap_ = now;
    }
    if (packets_.empty())
        gap_ = Time{};
    return result;
}
}  // namespace cosmo::service::gb
