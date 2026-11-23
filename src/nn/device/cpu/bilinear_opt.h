#pragma once
#include <cstring>

#include "nn/device/cpu/bilinear_neon.h"
static void BilinearResizeNeon4x3Opt(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    struct XMap {
        int left, right;
        float frac;
    };
    std::vector<XMap> map(dw);
    const float xr = float(sw) / dw, yr = float(sh) / dh;
    for (int x = 0; x < dw; ++x) {
        float fx = std::clamp((x + .5f) * xr - .5f, 0.f, float(sw - 1));
        int sx   = int(fx);
        float wx = fx - sx;
        sx       = std::max(0, std::min(sx, sw - 1));
        map[x]   = {sx, std::min(sx + 1, sw - 1), wx};
    }
    for (int y = 0; y < dh; ++y) {
        float fy      = std::clamp((y + .5f) * yr - .5f, 0.f, float(sh - 1));
        int sy        = int(fy);
        float wy      = fy - sy;
        sy            = std::max(0, std::min(sy, sh - 1));
        const int sy1 = std::min(sy + 1, sh - 1);
        auto wyv = vdupq_n_f32(wy), hyv = vdupq_n_f32(1 - wy);
        for (int x = 0; x < dw; x += 4) {
            uint8_t p00[24]{}, p01[24]{}, p10[24]{}, p11[24]{};
            float frac[4];
            for (int z = 0; z < 4; ++z) {
                const auto& m    = map[x + z];
                frac[z]          = m.frac;
                const uint8_t *a = src + (sy * sw + m.left) * 3, *b = src + (sy * sw + m.right) * 3,
                              *c = src + (sy1 * sw + m.left) * 3, *d = src + (sy1 * sw + m.right) * 3;
                for (int j = 0; j < 3; ++j) {
                    p00[z * 3 + j] = a[j];
                    p01[z * 3 + j] = b[j];
                    p10[z * 3 + j] = c[j];
                    p11[z * 3 + j] = d[j];
                }
            }
            const auto w = vld1q_f32(frac), h = vsubq_f32(vdupq_n_f32(1.f), w);
            auto a = vld3_u8(p00), b = vld3_u8(p01), c = vld3_u8(p10), d = vld3_u8(p11);
            uint8x8x3_t result;
            for (int j = 0; j < 3; ++j) {
                auto cv = [](uint8x8_t v) { return vcvtq_f32_u32(vmovl_u16(vget_low_u16(vmovl_u8(v)))); };
                auto aa = vmulq_f32(cv(a.val[j]), h), bb = vmulq_f32(cv(b.val[j]), w),
                     cc = vmulq_f32(cv(c.val[j]), h), dd = vmulq_f32(cv(d.val[j]), w);
                auto v        = vmulq_f32(bb, hyv);
                v             = vfmaq_f32(v, aa, hyv);
                v             = vfmaq_f32(v, cc, wyv);
                v             = vfmaq_f32(v, dd, wyv);
                v             = vaddq_f32(v, vdupq_n_f32(.5f));
                v             = vminq_f32(vdupq_n_f32(255.f), vmaxq_f32(vdupq_n_f32(0.f), v));
                auto u16      = vqmovn_u32(vcvtq_u32_f32(v));
                result.val[j] = vqmovn_u16(vcombine_u16(u16, vdup_n_u16(0)));
            }
            uint8x16x3_t tab{vcombine_u8(result.val[0], vdup_n_u8(0)),
                             vcombine_u8(result.val[1], vdup_n_u8(0)),
                             vcombine_u8(result.val[2], vdup_n_u8(0))};
            const uint8x16_t idx = {0, 16, 32, 1, 17, 33, 2, 18, 34, 3, 19, 35, 255, 255, 255, 255};
            uint8x16_t packed    = vqtbl3q_u8(tab, idx);
            uint8_t* outp        = dst + (y * dw + x) * 3;
            vst1_u8(outp, vget_low_u8(packed));
            uint32_t last = vgetq_lane_u32(vreinterpretq_u32_u8(packed), 2);
            std::memcpy(outp + 8, &last, 4);
        }
    }
}
