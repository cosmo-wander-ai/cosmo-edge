#include "nn/device/naive/naive_compute.h"

#include <algorithm>
#include <cmath>

namespace cosmo::nn {

void NaiveYUVToBGROrBGRALoop(const unsigned char* yptr0, const unsigned char* yptr1,
                             const unsigned char* vuptr, unsigned char* rgb0, unsigned char* rgb1, int remain,
                             bool is_nv12, int channel) {
    for (; remain > 0; remain -= 2) {
        int u, v;
        if (is_nv12) {
            u = (vuptr[0] > 240 ? 240 : vuptr[0]) - 128;
            v = (vuptr[1] > 240 ? 240 : vuptr[1]) - 128;
        } else {
            v = (vuptr[0] > 240 ? 240 : vuptr[0]) - 128;
            u = (vuptr[1] > 240 ? 240 : vuptr[1]) - 128;
        }

        int ruv = 102 * v;
        int guv = -52 * v + -25 * u;
        int buv = 129 * u;

#define SATURATE_CAST_UCHAR(X) (unsigned char)std::min(std::max(X, 0), 255);

        int y00 = yptr0[0] * 74 - 1135;

        if (channel == 4)
            rgb0[3] = 255;

        rgb0[0 * channel + 2] = SATURATE_CAST_UCHAR((y00 + ruv) >> 6);
        rgb0[0 * channel + 1] = SATURATE_CAST_UCHAR((y00 + guv) >> 6);
        rgb0[0 * channel + 0] = SATURATE_CAST_UCHAR((y00 + buv) >> 6);

        int y01 = yptr0[1] * 74 - 1135;

        if (channel == 4)
            rgb0[7] = 255;

        rgb0[1 * channel + 2] = SATURATE_CAST_UCHAR((y01 + ruv) >> 6);
        rgb0[1 * channel + 1] = SATURATE_CAST_UCHAR((y01 + guv) >> 6);
        rgb0[1 * channel + 0] = SATURATE_CAST_UCHAR((y01 + buv) >> 6);

        int y10 = yptr1[0] * 74 - 1135;
        if (channel == 4)
            rgb1[3] = 255;

        rgb1[0 * channel + 2] = SATURATE_CAST_UCHAR((y10 + ruv) >> 6);
        rgb1[0 * channel + 1] = SATURATE_CAST_UCHAR((y10 + guv) >> 6);
        rgb1[0 * channel + 0] = SATURATE_CAST_UCHAR((y10 + buv) >> 6);

        int y11 = yptr1[1] * 74 - 1135;
        if (channel == 4)
            rgb1[7] = 255;

        rgb1[1 * channel + 2] = SATURATE_CAST_UCHAR((y11 + ruv) >> 6);
        rgb1[1 * channel + 1] = SATURATE_CAST_UCHAR((y11 + guv) >> 6);
        rgb1[1 * channel + 0] = SATURATE_CAST_UCHAR((y11 + buv) >> 6);

#undef SATURATE_CAST_UCHAR

        yptr0 += 2;
        yptr1 += 2;
        vuptr += 2;
        rgb0 += 2 * channel;
        rgb1 += 2 * channel;
    }
}

void NaiveYUVToBGROrBGRA(const unsigned char* yuv, unsigned char* bgr, int w, int h, int c, bool is_nv12) {
    const unsigned char* y_ptr  = yuv;
    const unsigned char* vu_ptr = yuv + w * h;

    for (int y = 0; y < h; y += 2) {
        const unsigned char* y_ptr0 = y_ptr;
        const unsigned char* y_ptr1 = y_ptr + w;

        unsigned char* bgr0 = bgr;
        unsigned char* bgr1 = bgr + w * c;

        NaiveYUVToBGROrBGRALoop(y_ptr0, y_ptr1, vu_ptr, bgr0, bgr1, w, is_nv12, c);

        y_ptr += 2 * w;
        vu_ptr += w;
        bgr += 2 * w * c;
    }
}

void NaiveColorToGray(const unsigned char* src, unsigned char* dst, int w, int h, int channel,
                      bool bgr_order) {
    int offset = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned c1 = src[offset * channel + 0];
            unsigned c2 = src[offset * channel + 1];
            unsigned c3 = src[offset * channel + 2];

            unsigned b = bgr_order ? c1 : c3;
            unsigned g = c2;
            unsigned r = bgr_order ? c3 : c1;

            float gray_color = 0.114f * b + 0.587 * g + 0.299 * r;

            dst[offset] = gray_color;
            offset += 1;
        }
    }
}

void NaiveBGROrBGRAToGray(const unsigned char* src, unsigned char* dst, int w, int h, int channel) {
    NaiveColorToGray(src, dst, w, h, channel, true);
}

void NaiveRGBOrRGBAToGray(const unsigned char* src, unsigned char* dst, int w, int h, int channel) {
    NaiveColorToGray(src, dst, w, h, channel, false);
}

/*
 * Convert a nchw float mat to/from nchw float blob
 */
void NCHWConvert(const float* src, float* dst, float* mean, float* scale, int channel, int hw) {
    for (int c = 0; c < channel; ++c) {
        for (int i = 0; i < hw; ++i) {
            int data_pos  = c * hw + i;
            dst[data_pos] = scale[c] * (src[data_pos] + mean[c]);
        }
    }
}

/**
 * swap channel in format rgb uint8
 */
void NaiveRGBChannelSwap(unsigned char* src, unsigned char* dst, int hw) {
    for (int i = 0; i < hw; i++) {
        unsigned char tmp = src[i * 3];
        dst[i * 3]        = src[i * 3 + 2];
        dst[i * 3 + 1]    = src[i * 3 + 1];
        dst[i * 3 + 2]    = tmp;
    }
}

/**
 * swap channel in format rgba uint8
 */
void NaiveRGBAChannelSwap(unsigned char* src, unsigned char* dst, int hw) {
    for (int i = 0; i < hw; i++) {
        unsigned char tmp = src[i * 4];
        dst[i * 4]        = src[i * 4 + 2];
        dst[i * 4 + 1]    = src[i * 4 + 1];
        dst[i * 4 + 2]    = tmp;
        dst[i * 4 + 3]    = src[i * 4 + 3];
    }
}

/*
 * Convert an uint8 RGB / RGBA image to nchw float blob
 */
void RGBAToBlob(const unsigned char* src, float* dst, float* mean, float* scale, int channel, int hw) {
    auto dst_c0 = dst, dst_c1 = dst + hw;
    auto dst_c2 = dst + hw * 2, dst_c3 = dst + hw * 3;
    for (int i = 0; i < hw; i++) {
        dst_c0[i] = scale[0] * (src[4 * i + 0] - mean[0]);
        dst_c1[i] = scale[1] * (src[4 * i + 1] - mean[1]);
        dst_c2[i] = scale[2] * (src[4 * i + 2] - mean[2]);
        if (channel == 4)
            dst_c3[i] = scale[3] * (src[4 * i + 3] - mean[3]);
    }
}

/*
 * Convert an uint8 RGB image to nchw float blob
 */
void RGBToBlob(const unsigned char* src, float* dst, float* mean, float* scale, int hw) {
    auto dst_c0 = dst, dst_c1 = dst + hw, dst_c2 = dst + hw * 2;
    for (int i = 0; i < hw; ++i) {
        dst_c0[i] = scale[0] * (src[3 * i + 0] - mean[0]);
        dst_c1[i] = scale[1] * (src[3 * i + 1] - mean[1]);
        dst_c2[i] = scale[2] * (src[3 * i + 2] - mean[2]);
    }
}

/*
 * Convert an uint8 single channel image to nchw float blob
 */
void GrayToBlob(const unsigned char* src, float* dst, float mean, float scale, int hw) {
    for (int i = 0; i < hw; ++i) {
        dst[i] = scale * (src[i] - mean);
    }
}

static unsigned char saturate_cast(float data) {
    data += 0.5;
    data = std::min(std::max(data, 0.0f), 255.0f);
    return static_cast<unsigned char>(data);
}

/*
 * Convert a nchw float blob to BGRA
 * input blob must have 3 or 4 channels
 */
void BlobToBGRA(const float* src, unsigned char* dst, float* mean, float* scale, int hw) {
    auto src_c0 = src, src_c1 = src + hw;
    auto src_c2 = src + hw * 2, src_c3 = src + hw * 3;
    for (int i = 0; i < hw; ++i) {
        dst[4 * i + 0] = saturate_cast(scale[0] * (src_c0[i] - mean[0]));
        dst[4 * i + 1] = saturate_cast(scale[1] * (src_c1[i] - mean[1]));
        dst[4 * i + 2] = saturate_cast(scale[2] * (src_c2[i] - mean[2]));
        dst[4 * i + 3] = saturate_cast(scale[3] * (src_c3[i] - mean[3]));
    }
}

/*
 * Convert a nchw float blob to bgr uint8 image
 * input blob must have 3 channel
 */
void BlobToBGR(const float* src, unsigned char* dst, float* mean, float* scale, int hw) {
    auto src_c0 = src, src_c1 = src + hw, src_c2 = src + hw * 2;
    for (int i = 0; i < hw; ++i) {
        dst[3 * i + 0] = saturate_cast(scale[0] * (src_c0[i] - mean[0]));
        dst[3 * i + 1] = saturate_cast(scale[1] * (src_c1[i] - mean[1]));
        dst[3 * i + 2] = saturate_cast(scale[2] * (src_c2[i] - mean[2]));
    }
}

/*
 * Convert a nchw float blob to grayscale uint8 image
 * input blob must have only 1 channel
 */
void BlobToGray(const float* src, unsigned char* dst, float mean, float scale, int hw) {
    for (int i = 0; i < hw; ++i) {
        dst[i] = saturate_cast(scale * (src[i] - mean));
    }
}

}  // namespace cosmo::nn