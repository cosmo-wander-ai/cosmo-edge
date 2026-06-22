#pragma once

namespace cosmo::nn {

void NaiveYUVToBGROrBGRALoop(const unsigned char* yptr0, const unsigned char* yptr1,
                             const unsigned char* vuptr, unsigned char* bgr0, unsigned char* bgr1, int remain,
                             bool is_nv12, int channel);

void NaiveYUVToBGROrBGRA(const unsigned char* yuv, unsigned char* bgr, int w, int h, int channel,
                         bool is_nv12);

void NaiveColorToGray(const unsigned char* src, unsigned char* dst, int w, int h, int channel,
                      bool bgr_order);

void NaiveBGROrBGRAToGray(const unsigned char* src, unsigned char* dst, int w, int h, int channel);

void NaiveRGBOrRGBAToGray(const unsigned char* src, unsigned char* dst, int w, int h, int channel);

/**
 * swap channel in format rgb uint8
 */
void NaiveRGBChannelSwap(unsigned char* src, unsigned char* dst, int hw);

/**
 * swap channel in format rgba uint8
 */
void NaiveRGBAChannelSwap(unsigned char* src, unsigned char* dst, int hw);

/*
 * Convert a nchw float mat to/from nchw float blob
 */
void NCHWConvert(const float* src, float* dst, float* mean, float* scale, int channel, int hw);

/*
 * Convert an uint8 RGB / RGBA image to nchw float blob
 */
void RGBAToBlob(const unsigned char* src, float* dst, float* mean, float* scale, int channel, int hw);

/*
 * Convert an uint8 RGB image to nchw float blob
 */
void RGBToBlob(const unsigned char* src, float* dst, float* mean, float* scale, int hw);

/*
 * Convert an uint8 single channel image to nchw float blob
 */
void GrayToBlob(const unsigned char* src, float* dst, float mean, float scale, int hw);

/*
 * Convert a nchw float blob to BGRA
 * input blob must have 3 or 4 channels
 */
void BlobToBGRA(const float* src, unsigned char* dst, float* mean, float* scale, int hw);

/*
 * Convert a nchw float blob to bgr uint8 image
 * input blob must have 3 channel
 */
void BlobToBGR(const float* src, unsigned char* dst, float* mean, float* scale, int hw);

/*
 * Convert a nchw float blob to grayscale uint8 image
 * input blob must have only 1 channel
 */
void BlobToGray(const float* src, unsigned char* dst, float mean, float scale, int hw);

}  // namespace cosmo::nn
