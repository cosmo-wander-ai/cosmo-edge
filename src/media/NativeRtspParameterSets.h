#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cosmo::media {
namespace native_rtsp {

    struct Nal {
        size_t start;
        size_t payload;
        size_t end;
    };

    inline size_t StartCode(const std::vector<uint8_t>& bytes, size_t pos, size_t& length) {
        for (size_t i = pos; i + 3 <= bytes.size(); ++i) {
            if (bytes[i] != 0 || bytes[i + 1] != 0)
                continue;
            if (i + 4 <= bytes.size() && bytes[i + 2] == 0 && bytes[i + 3] == 1) {
                length = 4;
                return i;
            }
            if (bytes[i + 2] == 1) {
                length = 3;
                return i;
            }
        }
        length = 0;
        return bytes.size();
    }

    inline bool Split(const std::vector<uint8_t>& bytes, std::vector<Nal>& nals) {
        size_t prefix = 0;
        size_t start  = StartCode(bytes, 0, prefix);
        if (start == bytes.size())
            return false;
        for (size_t i = 0; i < start; ++i)
            if (bytes[i] != 0)
                return false;
        while (start < bytes.size()) {
            const size_t payload = start + prefix;
            size_t next_prefix   = 0;
            const size_t next    = StartCode(bytes, payload, next_prefix);
            if (payload >= next || (bytes[payload] & 0x80))
                return false;
            nals.push_back({start, payload, next});
            start  = next;
            prefix = next_prefix;
        }
        return !nals.empty();
    }

    // Call for the first admitted RTSP H.264 key packet before creating its witness.
    // Only Annex B SDP parameter sets are accepted; the normal color parser still
    // verifies all parameter sets and rejects changes or inconsistent slice headers.
    inline bool AddMissingParameterSets(const uint8_t* extra, size_t extra_size, std::vector<uint8_t>& au) {
        std::vector<Nal> access;
        if (!Split(au, access))
            return false;
        bool has_sps = false, has_pps = false;
        for (const auto& nal : access) {
            const auto type = au[nal.payload] & 31;
            has_sps |= type == 7;
            has_pps |= type == 8;
        }
        if (has_sps && has_pps)
            return true;
        if (!extra || extra_size == 0 || extra_size > 8192)
            return false;
        const std::vector<uint8_t> sets(extra, extra + extra_size);
        std::vector<Nal> parameters;
        if (!Split(sets, parameters))
            return false;
        bool extra_sps = false, extra_pps = false;
        for (const auto& nal : parameters) {
            const auto type = sets[nal.payload] & 31;
            if (type != 7 && type != 8)
                return false;
            extra_sps |= type == 7;
            extra_pps |= type == 8;
        }
        if (!extra_sps || !extra_pps)
            return false;
        // AUD, if present, must remain the first NAL in an access unit.
        const size_t offset = (au[access[0].payload] & 31) == 9 ? access[0].end : access[0].start;
        au.insert(au.begin() + offset, sets.begin(), sets.end());
        return true;
    }

}  // namespace native_rtsp
}  // namespace cosmo::media
