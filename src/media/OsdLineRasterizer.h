#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "media/Color.h"
#include "media/IOsdTextRenderer.h"
#include "util/Point.h"
#include "util/Rect.h"

namespace cosmo::media {

// A set of source-frame pixel coordinates with one shared appearance.
struct OsdLineGroup {
    std::vector<std::pair<util::Point, util::Point>> lines;
    Color color;
    int width{2};
    bool fine{false};  // fine target geometry omits the legacy dark halo
};

// Composite antialiased lines directly into contiguous I420 planes. Luma is
// blended per pixel; chroma coverage is averaged over each 2x2 luma block.
// Lines are clipped to the image, with no additional image-border edges.
// The caller owns the planes and must provide width*height luma bytes and
// (width/2)*(height/2) bytes for each chroma plane.
void RasterizeOsdLinesI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height,
                           const std::vector<OsdLineGroup>& groups);

void RasterizeOsdLinesRgb(uint8_t* rgb, int width, int height, bool bgr,
                          const std::vector<OsdLineGroup>& groups);

// Clip and blend a filled source-pixel rectangle, averaging coverage for
// chroma samples at rectangle boundaries.
void RasterizeOsdRectI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height, const util::Box& rect,
                          Color color, uint8_t opacity);

void RasterizeOsdRoundedRectI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height,
                                 const util::Box& rect, Color color, uint8_t opacity, int radius);
void RasterizeOsdRoundedRectRgb(uint8_t* rgb, int width, int height, bool bgr, const util::Box& rect,
                                Color color, uint8_t opacity, int radius);

// A 1px antialiased ring inside the rounded boundary. Interior pixels are not blended.
// I420 chroma uses the mean ring coverage of each 2x2 luma block.
void RasterizeOsdRoundedRectOutlineI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height,
                                        const util::Box& rect, Color color, uint8_t opacity, int radius);
void RasterizeOsdRoundedRectOutlineRgb(uint8_t* rgb, int width, int height, bool bgr, const util::Box& rect,
                                       Color color, uint8_t opacity, int radius);

// The bitmap offset is applied to the source-pixel text origin. Each chroma
// sample is blended once using the mean coverage of its four luma pixels.
void RasterizeOsdTextI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height, int x, int top,
                          const IOsdTextRenderer::OutlinedTextBitmap& bitmap, Color color);

}  // namespace cosmo::media
