#pragma once
// Compressed-byte inspection only: admit the tested fixed-size JFIF/YCbCr
// baseline profile. Other dimensions, color profiles and orientation-bearing
// metadata require a separate native conversion route.
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace cosmo::face_landmark_candidate {
inline bool AdmitJpeg601Full(const std::uint8_t* b, std::size_t n) noexcept {
    if (!b || n < 4 || b[0] != 0xff || b[1] != 0xd8)
        return false;
    bool jfif = false, sof = false;
    std::size_t p = 2;
    while (p < n) {
        if (b[p++] != 0xff)
            return false;
        while (p < n && b[p] == 0xff)
            ++p;
        if (p >= n)
            return false;
        const auto marker = b[p++];
        if (marker == 0xd9 || marker == 0xd8 || marker == 0x00 || (marker >= 0xd0 && marker <= 0xd7))
            return false;
        if (p + 2 > n)
            return false;
        const std::size_t len = (std::size_t(b[p]) << 8) | b[p + 1];
        if (len < 2 || len > n - p)
            return false;
        const auto* payload = b + p + 2;
        const auto size     = len - 2;
        if (marker == 0xda)
            return jfif && sof && size >= 10;  // entropy is validated by hardware decoder
        if (marker == 0xe0 && size >= 5 && std::memcmp(payload, "JFIF\0", 5) == 0)
            jfif = true;
        if (marker == 0xe1 || marker == 0xe2 || marker == 0xee)
            return false;  // orientation, ICC or Adobe transform
        if ((marker >= 0xc0 && marker <= 0xcf) && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (marker != 0xc0 || sof || size != 15 || payload[0] != 8 || payload[1] != 0x04 ||
                payload[2] != 0x38 || payload[3] != 0x07 || payload[4] != 0x80 || payload[5] != 3)
                return false;
            if (payload[6] != 1 || payload[7] != 0x22 || payload[9] != 2 || payload[10] != 0x11 ||
                payload[12] != 3 || payload[13] != 0x11)
                return false;
            sof = true;
        }
        p += len;
    }
    return false;
}
}  // namespace cosmo::face_landmark_candidate
