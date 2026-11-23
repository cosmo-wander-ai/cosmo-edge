#pragma once
#include "media/i420_bt601_neon.h"
namespace cosmo::media::detail {
// Direct semiplanar ROI conversion; image coordinates are relative to the
// visible crop and chroma sampling remains aligned to the decoded frame.
inline void Nv12RoiBt601ToBgr(const uint8_t* y, size_t y_stride, const uint8_t* uv, size_t uv_stride,
                              uint8_t* dst, int crop_x, int crop_y, int x, int y0, int rw, int rh, bool vu) {
    for (int row = 0; row < rh; row++) {
        const int py = crop_y + y0 + row;
        int col      = 0;
        auto one     = [&](int i) {
            const int px = crop_x + x + i;
            auto pair    = uv + (py / 2) * uv_stride + (px / 2) * 2;
            const int c = y[py * y_stride + px] - 16, d = pair[vu ? 1 : 0] - 128, e = pair[vu ? 0 : 1] - 128,
                      di = (row * rw + i) * 3;
            dst[di]      = ClampBt601((298 * c + 516 * d + 128) >> 8);
            dst[di + 1]  = ClampBt601((298 * c - 100 * d - 208 * e + 128) >> 8);
            dst[di + 2]  = ClampBt601((298 * c + 409 * e + 128) >> 8);
        };
#if defined(__aarch64__) && defined(__AARCH64EL__)
        if (((crop_x + x) & 1) && col < rw)
            one(col++);
        for (; col + 8 <= rw; col += 8) {
            int px = crop_x + x + col;
            uint64_t packed;
            std::memcpy(&packed, uv + (py / 2) * uv_stride + px, 8);
            const auto z = vcreate_u8(packed);
            auto a = vuzp1_u8(z, z), b = vuzp2_u8(z, z);
            uint32_t u32 = vget_lane_u32(vreinterpret_u32_u8(vu ? b : a), 0),
                     v32 = vget_lane_u32(vreinterpret_u32_u8(vu ? a : b), 0);
            auto terms =
                I420Terms(reinterpret_cast<const uint8_t*>(&u32), reinterpret_cast<const uint8_t*>(&v32));
            I420Store8(y + py * y_stride + px, dst + (row * rw + col) * 3, terms, true);
        }
#endif
        for (; col < rw; col++)
            one(col);
    }
}
}  // namespace cosmo::media::detail
