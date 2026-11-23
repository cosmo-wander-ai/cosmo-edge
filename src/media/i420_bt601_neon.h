#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#if defined(__aarch64__) && defined(__AARCH64EL__)
#include <arm_neon.h>
#endif

namespace cosmo::media::detail {
inline uint8_t ClampBt601(int x) {
    return static_cast<uint8_t>(std::clamp(x, 0, 255));
}

#if defined(__aarch64__) && defined(__AARCH64EL__)
inline uint8x8_t I420Narrow(int32x4_t low, int32x4_t high) {
    return vqmovn_u16(vcombine_u16(vqmovun_s32(vshrq_n_s32(low, 8)), vqmovun_s32(vshrq_n_s32(high, 8))));
}
struct I420ChromaTerms {
    int32x4_t rl, rh, gl, gh, bl, bh;
};
inline I420ChromaTerms I420Terms(const uint8_t* u, const uint8_t* v) {
    uint32_t uv, vv;
    std::memcpy(&uv, u, 4);
    std::memcpy(&vv, v, 4);
    const uint8x8_t u8 = vcreate_u8(uv), v8 = vcreate_u8(vv);
    const int16x8_t du = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vzip1_u8(u8, u8))), vdupq_n_s16(128));
    const int16x8_t dv = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vzip1_u8(v8, v8))), vdupq_n_s16(128));
    const int32x4_t ul = vmovl_s16(vget_low_s16(du)), uh = vmovl_s16(vget_high_s16(du));
    const int32x4_t vl = vmovl_s16(vget_low_s16(dv)), vh = vmovl_s16(vget_high_s16(dv));
    const int32x4_t bias = vdupq_n_s32(128);
    return {vmlaq_n_s32(bias, vl, 409),
            vmlaq_n_s32(bias, vh, 409),
            vmlsq_n_s32(vmlsq_n_s32(bias, ul, 100), vl, 208),
            vmlsq_n_s32(vmlsq_n_s32(bias, uh, 100), vh, 208),
            vmlaq_n_s32(bias, ul, 516),
            vmlaq_n_s32(bias, uh, 516)};
}
inline void I420Store8(const uint8_t* y, uint8_t* out, const I420ChromaTerms& t, bool bgr) {
    const int16x8_t c  = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(y))), vdupq_n_s16(16));
    const int32x4_t yl = vmulq_n_s32(vmovl_s16(vget_low_s16(c)), 298);
    const int32x4_t yh = vmulq_n_s32(vmovl_s16(vget_high_s16(c)), 298);
    const uint8x8_t r  = I420Narrow(vaddq_s32(yl, t.rl), vaddq_s32(yh, t.rh));
    const uint8x8_t g  = I420Narrow(vaddq_s32(yl, t.gl), vaddq_s32(yh, t.gh));
    const uint8x8_t b  = I420Narrow(vaddq_s32(yl, t.bl), vaddq_s32(yh, t.bh));
    uint8x8x3_t rgb;
    rgb.val[0] = bgr ? b : r;
    rgb.val[1] = g;
    rgb.val[2] = bgr ? r : b;
    vst3_u8(out, rgb);
}
#endif

// Matches the scalar BT.601 limited-range formula, including negative shifts,
// rounding and saturation. Chroma is sampled on the same 2x2 pixel grid.
inline void I420Bt601ToPacked(const uint8_t* y, const uint8_t* u, const uint8_t* v, uint8_t* dst, int width,
                              int height, bool bgr) {
    const int hw = width / 2;
    for (int row = 0; row < height; ++row) {
        int col = 0;
#if defined(__aarch64__) && defined(__AARCH64EL__)
        for (; col + 8 <= width; col += 8) {
            auto terms = I420Terms(u + (row / 2) * hw + col / 2, v + (row / 2) * hw + col / 2);
            I420Store8(y + row * width + col, dst + (row * width + col) * 3, terms, bgr);
        }
#endif
        for (; col < width; ++col) {
            int c = y[row * width + col] - 16, d = u[(row / 2) * hw + col / 2] - 128,
                e      = v[(row / 2) * hw + col / 2] - 128;
            uint8_t r  = ClampBt601((298 * c + 409 * e + 128) >> 8);
            uint8_t g  = ClampBt601((298 * c - 100 * d - 208 * e + 128) >> 8);
            uint8_t b  = ClampBt601((298 * c + 516 * d + 128) >> 8);
            int i      = (row * width + col) * 3;
            dst[i]     = bgr ? b : r;
            dst[i + 1] = g;
            dst[i + 2] = bgr ? r : b;
        }
    }
}
}  // namespace cosmo::media::detail
