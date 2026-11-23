#pragma once
#include "media/i420_bt601_neon.h"
namespace cosmo::media::detail {
// Only pair rows which share the same source UV row; preserve the reference
// scalar prologue/tail and exact arithmetic for all origin and ROI parities.
inline void Nv12RoiBt601ToBgr2Rows(const uint8_t* y, size_t ys, const uint8_t* uv, size_t us, uint8_t* dst,
                                   int cx, int cy, int x, int y0, int rw, int rh, bool vu) {
    for (int row = 0; row < rh;) {
        const int py   = cy + y0 + row;
        const bool two = (py % 2 == 0 && row + 1 < rh);
        int col        = 0;
        auto one       = [&](int r, int i) {
            const int ay = cy + y0 + r, px = cx + x + i;
            auto pair   = uv + (ay / 2) * us + (px / 2) * 2;
            const int c = y[ay * ys + px] - 16, d = pair[vu ? 1 : 0] - 128, e = pair[vu ? 0 : 1] - 128,
                      di = (r * rw + i) * 3;
            dst[di]      = ClampBt601((298 * c + 516 * d + 128) >> 8);
            dst[di + 1]  = ClampBt601((298 * c - 100 * d - 208 * e + 128) >> 8);
            dst[di + 2]  = ClampBt601((298 * c + 409 * e + 128) >> 8);
        };
#if defined(__aarch64__) && defined(__AARCH64EL__)
        if (((cx + x) & 1) && col < rw) {
            one(row, col);
            if (two)
                one(row + 1, col);
            ++col;
        }
        for (; col + 8 <= rw; col += 8) {
            const int px = cx + x + col;
            uint64_t packed;
            std::memcpy(&packed, uv + (py / 2) * us + px, 8);
            const auto z = vcreate_u8(packed);
            auto a = vuzp1_u8(z, z), b = vuzp2_u8(z, z);
            uint32_t u32 = vget_lane_u32(vreinterpret_u32_u8(vu ? b : a), 0),
                     v32 = vget_lane_u32(vreinterpret_u32_u8(vu ? a : b), 0);
            auto terms =
                I420Terms(reinterpret_cast<const uint8_t*>(&u32), reinterpret_cast<const uint8_t*>(&v32));
            I420Store8(y + py * ys + px, dst + (row * rw + col) * 3, terms, true);
            if (two)
                I420Store8(y + (py + 1) * ys + px, dst + ((row + 1) * rw + col) * 3, terms, true);
        }
#endif
        for (; col < rw; col++) {
            one(row, col);
            if (two)
                one(row + 1, col);
        }
        row += two ? 2 : 1;
    }
}
}  // namespace cosmo::media::detail
