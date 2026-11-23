#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "media/VideoPacket.h"

namespace cosmo::native_raw_preview {
constexpr size_t kQueuePackets   = 30;
constexpr size_t kMaxPacketBytes = 2 * 1024 * 1024;

// Accept the H.264 Baseline profile only. B pictures are not part of the
// Baseline profile; other profiles need separate timestamp support and tests.
inline bool IsBaselineConfig(const std::vector<uint8_t>& bytes) {
    bool found = false;
    if (bytes.size() >= 7 && bytes[0] == 1) {
        size_t offset        = 6;
        const unsigned count = bytes[5] & 31;
        for (unsigned i = 0; i < count; ++i) {
            if (offset + 2 > bytes.size())
                return false;
            const size_t size = (size_t(bytes[offset]) << 8) | bytes[offset + 1];
            offset += 2;
            if (size < 4 || size > bytes.size() - offset || (bytes[offset] & 31) != 7 ||
                bytes[offset + 1] != 66)
                return false;
            found = true;
            offset += size;
        }
        return found;
    }
    for (size_t i = 0; i + 4 < bytes.size(); ++i) {
        size_t n = 0;
        if (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1)
            n = i + 3;
        else if (i + 5 < bytes.size() && bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 0 &&
                 bytes[i + 3] == 1)
            n = i + 4;
        if (n && (bytes[n] & 31) == 7) {
            if (n + 1 >= bytes.size() || bytes[n + 1] != 66)
                return false;
            found = true;
            i     = n;
        }
    }
    return found;
}

inline VideoPacketPtr CopyPacket(const VideoPacketPtr& source) {
    if (!source || source->data.empty() || source->data.size() > kMaxPacketBytes)
        return {};
    auto copy = std::make_shared<media::VideoPacket>(*source);
    // The viewer consumes only compressed bytes and metadata; it does not own
    // the native decoder's authority witness.
    copy->color_witness.reset();
    return copy;
}
}  // namespace cosmo::native_raw_preview
