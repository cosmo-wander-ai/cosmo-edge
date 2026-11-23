#pragma once
#include <arm_neon.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// Experimental, NHWC BGR/RGB with exactly 3 channels. Uses the original
// AArch64 GCC contraction order and leaves coordinate calculation scalar.
static inline float32x4_t load3f(const uint8_t* p) {
    const uint64_t packed = uint64_t(p[0]) | (uint64_t(p[1]) << 8) | (uint64_t(p[2]) << 16);
    uint8x8_t bytes       = vreinterpret_u8_u64(vcreate_u64(packed));
    uint16x8_t u16        = vmovl_u8(bytes);
    return vcvtq_f32_u32(vmovl_u16(vget_low_u16(u16)));
}
static void BilinearResizeNeon3(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    const float xr = float(sw) / dw, yr = float(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        float fy       = std::clamp((y + 0.5f) * yr - 0.5f, 0.0f, float(sh - 1));
        int sy         = int(fy);
        float wy       = fy - sy;
        sy             = std::max(0, std::min(sy, sh - 1));
        const int sy1  = std::min(sy + 1, sh - 1);
        const auto wyv = vdupq_n_f32(wy), hyv = vdupq_n_f32(1 - wy);
        for (int x = 0; x < dw; ++x) {
            float fx       = std::clamp((x + 0.5f) * xr - 0.5f, 0.0f, float(sw - 1));
            int sx         = int(fx);
            float wx       = fx - sx;
            sx             = std::max(0, std::min(sx, sw - 1));
            const int sx1  = std::min(sx + 1, sw - 1);
            const auto wxv = vdupq_n_f32(wx), hxv = vdupq_n_f32(1 - wx);
            auto a          = vmulq_f32(load3f(src + (sy * sw + sx) * 3), hxv);
            auto b          = vmulq_f32(load3f(src + (sy * sw + sx1) * 3), wxv);
            auto c          = vmulq_f32(load3f(src + (sy1 * sw + sx) * 3), hxv);
            auto d          = vmulq_f32(load3f(src + (sy1 * sw + sx1) * 3), wxv);
            auto v          = vmulq_f32(b, hyv);
            v               = vfmaq_f32(v, a, hyv);
            v               = vfmaq_f32(v, c, wyv);
            v               = vfmaq_f32(v, d, wyv);
            v               = vaddq_f32(v, vdupq_n_f32(.5f));
            v               = vminq_f32(vdupq_n_f32(255.f), vmaxq_f32(vdupq_n_f32(0.f), v));
            uint32x4_t u32  = vcvtq_u32_f32(v);
            uint16x4_t u16  = vqmovn_u32(u32);
            uint8x8_t u8    = vqmovn_u16(vcombine_u16(u16, vdup_n_u16(0)));
            uint32_t packed = vget_lane_u32(vreinterpret_u32_u8(u8), 0);
            uint8_t* q      = dst + (y * dw + x) * 3;
            q[0]            = packed;
            q[1]            = packed >> 8;
            q[2]            = packed >> 16;
        }
    }
}

// Four destination pixels per NEON vector; caller uses it only when dw%4==0.
static void BilinearResizeNeon4x3(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
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
            uint8_t p00[24]{}, p01[24]{}, p10[24]{}, p11[24]{}, out[24];
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
            vst3_u8(out, result);
            for (int k = 0; k < 12; ++k)
                dst[(y * dw + x) * 3 + k] = out[k];
        }
    }
}

// Cache x coordinates once per image; interpolation expression is unchanged.
static void BilinearResizeMapped3(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    struct XMap {
        int left, right;
        float frac;
    };
    std::vector<XMap> map(dw);
    const float xr = float(sw) / dw, yr = float(sh) / dh;
    for (int x = 0; x < dw; x++) {
        float fx = std::clamp((x + .5f) * xr - .5f, 0.f, float(sw - 1));
        int sx   = int(fx);
        float wx = fx - sx;
        sx       = std::max(0, std::min(sx, sw - 1));
        map[x]   = {sx, std::min(sx + 1, sw - 1), wx};
    }
    for (int y = 0; y < dh; y++) {
        float fy = std::clamp((y + .5f) * yr - .5f, 0.f, float(sh - 1));
        int sy   = int(fy);
        float wy = fy - sy;
        sy       = std::max(0, std::min(sy, sh - 1));
        int sy1  = std::min(sy + 1, sh - 1);
        for (int x = 0; x < dw; x++) {
            const auto& m = map[x];
            int sx = m.left, sx1 = m.right;
            float wx = m.frac;
            for (int c = 0; c < 3; c++) {
                float v00 = src[(sy * sw + sx) * 3 + c], v01 = src[(sy * sw + sx1) * 3 + c],
                      v10 = src[(sy1 * sw + sx) * 3 + c], v11 = src[(sy1 * sw + sx1) * 3 + c];
                float val =
                    v00 * (1 - wx) * (1 - wy) + v01 * wx * (1 - wy) + v10 * (1 - wx) * wy + v11 * wx * wy;
                dst[(y * dw + x) * 3 + c] = uint8_t(std::min(255.f, std::max(0.f, val + .5f)));
            }
        }
    }
}
