#include "media/OsdLineRasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace cosmo::media {
namespace {

    struct YuvColor {
        uint8_t y, u, v;
    };

    YuvColor ToYuv(Color c) {
        const float r = c.red, g = c.green, b = c.blue;
        return {
            static_cast<uint8_t>(std::clamp(0.299F * r + 0.587F * g + 0.114F * b, 0.0F, 255.0F)),
            static_cast<uint8_t>(std::clamp(128.0F - 0.169F * r - 0.331F * g + 0.500F * b, 0.0F, 255.0F)),
            static_cast<uint8_t>(std::clamp(128.0F + 0.500F * r - 0.419F * g - 0.081F * b, 0.0F, 255.0F))};
    }

    void Blend(uint8_t& dst, uint8_t src, unsigned alpha) {
        dst = static_cast<uint8_t>((alpha * src + (255U - alpha) * dst + 127U) / 255U);
    }

    // Clip the centre line to the image extended by the stroke radius. Clipping
    // caps the work even for malformed coordinates far outside the picture. The
    // extension preserves the visible part of thick strokes near the boundary.
    bool ClipLine(double& x0, double& y0, double& x1, double& y1, double left, double top, double right,
                  double bottom) {
        const double dx = x1 - x0;
        const double dy = y1 - y0;
        double first = 0.0, last = 1.0;
        const auto update = [&](double p, double q) {
            if (p == 0.0)
                return q >= 0.0;
            const double r = q / p;
            if (p < 0.0)
                first = std::max(first, r);
            else
                last = std::min(last, r);
            return first <= last;
        };
        if (!update(-dx, x0 - left) || !update(dx, right - x0) || !update(-dy, y0 - top) ||
            !update(dy, bottom - y0))
            return false;
        const double originalX = x0, originalY = y0;
        x0 = originalX + first * dx;
        y0 = originalY + first * dy;
        x1 = originalX + last * dx;
        y1 = originalY + last * dy;
        return true;
    }

    struct Line {
        double x0, y0, dx, dy, lengthSquared;
        double radius;
        unsigned opacity;

        unsigned Coverage(double x, double y) const {
            double t = 0.0;
            if (lengthSquared > 0.0)
                t = std::clamp(((x - x0) * dx + (y - y0) * dy) / lengthSquared, 0.0, 1.0);
            const double ex        = x - (x0 + t * dx);
            const double ey        = y - (y0 + t * dy);
            const double distance2 = ex * ex + ey * ey;
            const double outer     = radius + 0.5;
            if (distance2 >= outer * outer)
                return 0;
            const double inner = std::max(0.0, radius - 0.5);
            if (distance2 <= inner * inner)
                return opacity;
            const double coverage = std::clamp(outer - std::sqrt(distance2), 0.0, 1.0);
            return static_cast<unsigned>(std::lround(coverage * opacity));
        }
    };

    // Work in chroma-sized 2x2 blocks. Each block is visited once per segment so
    // the UV channels are blended once using the mean luma coverage, not repeatedly
    // by each of the four luma pixels.
    template <class Pixel, class Block>
    void DrawLineCoverage(int width, int height, const std::pair<util::Point, util::Point>& segment,
                          int strokeWidth, unsigned opacity, Pixel&& pixel, Block&& chroma) {
        if (strokeWidth <= 0 || opacity == 0)
            return;
        const int chromaWidth  = width / 2;
        const int chromaHeight = height / 2;
        // The I420 frame implementation uses floor-sized chroma planes. Odd edge
        // luma pixels are not part of a complete 2x2 block and are handled below.
        const int blocksWide = (width + 1) / 2;
        const int blocksHigh = (height + 1) / 2;
        const double radius  = std::min<double>(static_cast<double>(strokeWidth) / 2.0,
                                                static_cast<double>(std::max(width, height)) + 2.0);
        const double pad     = radius + 2.0;
        double x0 = segment.first.x, y0 = segment.first.y;
        double x1 = segment.second.x, y1 = segment.second.y;
        if (!ClipLine(x0, y0, x1, y1, -pad, -pad, width - 1.0 + pad, height - 1.0 + pad))
            return;
        const double dx = x1 - x0, dy = y1 - y0;
        const Line line{x0, y0, dx, dy, dx * dx + dy * dy, radius, opacity};

        const auto block = [&](int bx, int by) {
            unsigned sum = 0;
            for (int oy = 0; oy < 2; ++oy) {
                const int py = by * 2 + oy;
                if (py >= height)
                    continue;
                for (int ox = 0; ox < 2; ++ox) {
                    const int px = bx * 2 + ox;
                    if (px >= width)
                        continue;
                    const unsigned a = line.Coverage(px, py);
                    sum += a;
                    if (a != 0)
                        pixel(px, py, a);
                }
            }
            if (sum != 0 && bx < chromaWidth && by < chromaHeight) {
                const unsigned a = (sum + 2U) / 4U;
                chroma(bx, by, a);
            }
        };

        if (std::abs(dx) >= std::abs(dy)) {
            const double lo = std::max(0.0, std::floor((std::min(x0, x1) - pad) / 2.0));
            const double hi = std::min<double>(blocksWide - 1, std::ceil((std::max(x0, x1) + pad) / 2.0));
            if (lo > hi)
                return;
            const double slope = dx == 0.0 ? 0.0 : dy / dx;
            const double reach = radius * std::sqrt(1.0 + slope * slope) + 3.0;
            for (int bx = static_cast<int>(lo); bx <= static_cast<int>(hi); ++bx) {
                const double x      = bx * 2.0 + 0.5;
                const double t      = dx == 0.0 ? 0.0 : std::clamp((x - x0) / dx, 0.0, 1.0);
                const double center = y0 + t * dy;
                const double minY   = std::max(0.0, std::floor((center - reach) / 2.0));
                const double maxY   = std::min<double>(blocksHigh - 1, std::ceil((center + reach) / 2.0));
                for (int by = static_cast<int>(minY); by <= static_cast<int>(maxY); ++by)
                    block(bx, by);
            }
        } else {
            const double lo = std::max(0.0, std::floor((std::min(y0, y1) - pad) / 2.0));
            const double hi = std::min<double>(blocksHigh - 1, std::ceil((std::max(y0, y1) + pad) / 2.0));
            if (lo > hi)
                return;
            const double slope = dx / dy;
            const double reach = radius * std::sqrt(1.0 + slope * slope) + 3.0;
            for (int by = static_cast<int>(lo); by <= static_cast<int>(hi); ++by) {
                const double ypos   = by * 2.0 + 0.5;
                const double t      = std::clamp((ypos - y0) / dy, 0.0, 1.0);
                const double center = x0 + t * dx;
                const double minX   = std::max(0.0, std::floor((center - reach) / 2.0));
                const double maxX   = std::min<double>(blocksWide - 1, std::ceil((center + reach) / 2.0));
                for (int bx = static_cast<int>(minX); bx <= static_cast<int>(maxX); ++bx)
                    block(bx, by);
            }
        }
    }
}  // namespace

void RasterizeOsdLinesI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height,
                           const std::vector<OsdLineGroup>& groups) {
    if (!y || !u || !v || width <= 0 || height <= 0)
        return;
    // Limit dimensions to avoid integer overflow and impractically large work
    // when called with invalid buffers. Real video frames are far below this.
    if (width > 65535 || height > 65535)
        return;
    for (const auto& group : groups) {
        if (group.width <= 0)
            continue;
        const auto color = ToYuv(group.color);
        const YuvColor outline{16, 128, 128};
        const int outerWidth = group.width > std::numeric_limits<int>::max() - 2
                                   ? std::numeric_limits<int>::max()
                                   : group.width + 2;
        const auto draw      = [&](const auto& segment, int stroke, YuvColor c, unsigned opacity) {
            DrawLineCoverage(
                width, height, segment, stroke, opacity,
                [&](int px, int py, unsigned a) { Blend(y[static_cast<size_t>(py) * width + px], c.y, a); },
                [&](int bx, int by, unsigned a) {
                    const size_t index = static_cast<size_t>(by) * (width / 2) + bx;
                    Blend(u[index], c.u, a);
                    Blend(v[index], c.v, a);
                });
        };
        if (!group.fine)
            for (const auto& line : group.lines)
                draw(line, outerWidth, outline, 150);
        for (const auto& line : group.lines)
            draw(line, group.width, color, 255);
    }
}

void RasterizeOsdLinesRgb(uint8_t* rgb, int width, int height, bool bgr,
                          const std::vector<OsdLineGroup>& groups) {
    if (!rgb || width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return;
    for (const auto& group : groups) {
        const auto draw = [&](const auto& segment, int stroke, Color color, unsigned opacity) {
            const uint8_t c[3] = {bgr ? color.blue : color.red, color.green, bgr ? color.red : color.blue};
            DrawLineCoverage(
                width, height, segment, stroke, opacity,
                [&](int px, int py, unsigned a) {
                    for (int n = 0; n < 3; ++n)
                        Blend(rgb[(static_cast<size_t>(py) * width + px) * 3 + n], c[n], a);
                },
                [](int, int, unsigned) {});
        };
        if (!group.fine && group.width > 0) {
            const int outer = group.width > std::numeric_limits<int>::max() - 2
                                  ? std::numeric_limits<int>::max()
                                  : group.width + 2;
            for (const auto& line : group.lines)
                draw(line, outer, {16, 16, 16}, 150);
        }
        for (const auto& line : group.lines)
            draw(line, group.width, group.color, 255);
    }
}

void RasterizeOsdRectI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height, const util::Box& rect,
                          Color color, uint8_t opacity) {
    if (!y || !u || !v || width <= 0 || height <= 0 || width > 65535 || height > 65535 || rect.width <= 0 ||
        rect.height <= 0 || opacity == 0)
        return;
    const auto x0 = static_cast<int>(std::clamp<int64_t>(rect.x, 0, width));
    const auto y0 = static_cast<int>(std::clamp<int64_t>(rect.y, 0, height));
    const auto x1 = static_cast<int>(std::clamp<int64_t>(int64_t(rect.x) + rect.width, 0, width));
    const auto y1 = static_cast<int>(std::clamp<int64_t>(int64_t(rect.y) + rect.height, 0, height));
    if (x0 >= x1 || y0 >= y1)
        return;
    const auto c = ToYuv(color);
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px)
            Blend(y[static_cast<size_t>(py) * width + px], c.y, opacity);
    const int uvWidth = width / 2, uvHeight = height / 2;
    for (int uy = y0 / 2; uy < std::min(uvHeight, (y1 + 1) / 2); ++uy) {
        for (int ux = x0 / 2; ux < std::min(uvWidth, (x1 + 1) / 2); ++ux) {
            const int coverage = (std::min(x1, ux * 2 + 2) - std::max(x0, ux * 2)) *
                                 (std::min(y1, uy * 2 + 2) - std::max(y0, uy * 2));
            const unsigned alpha = (static_cast<unsigned>(opacity) * coverage + 2) / 4;
            const size_t index   = static_cast<size_t>(uy) * uvWidth + ux;
            Blend(u[index], c.u, alpha);
            Blend(v[index], c.v, alpha);
        }
    }
}

namespace {
    unsigned RoundedCoverage(const util::Box& rect, int radius, int x, int y, unsigned opacity) {
        const double r  = std::clamp<double>(radius, 0.0, std::min(rect.width, rect.height) / 2.0);
        const double px = x + 0.5, py = y + 0.5;
        if (px < rect.x || py < rect.y || px >= int64_t(rect.x) + rect.width ||
            py >= int64_t(rect.y) + rect.height)
            return 0;
        if (r == 0)
            return opacity;
        const double cx = std::clamp(px, rect.x + r, rect.x + double(rect.width) - r);
        const double cy = std::clamp(py, rect.y + r, rect.y + double(rect.height) - r);
        const double dx = px - cx, dy = py - cy;
        if (dx == 0 || dy == 0)
            return opacity;
        return static_cast<unsigned>(
            std::lround(std::clamp(r + 0.5 - std::hypot(dx, dy), 0.0, 1.0) * opacity));
    }

    unsigned RoundedOutlineCoverage(const util::Box& rect, int radius, int x, int y, unsigned opacity) {
        const double r = std::clamp<double>(radius, 0.0, std::min(rect.width, rect.height) / 2.0);
        // Most panel pixels are fully interior. Avoid distance/sqrt work there;
        // only the one-pixel edge and curved corner bands can contribute.
        const double band = std::max(1.0, r);
        const double px = x + 0.5, py = y + 0.5;
        const double right = double(rect.x) + rect.width, bottom = double(rect.y) + rect.height;
        if ((px >= rect.x + band && px <= right - band && py >= rect.y + 1.0 && py <= bottom - 1.0) ||
            (py >= rect.y + band && py <= bottom - band && px >= rect.x + 1.0 && px <= right - 1.0))
            return 0;
        const double halfWidth = rect.width / 2.0, halfHeight = rect.height / 2.0;
        const double qx = std::abs(x + 0.5 - rect.x - halfWidth) - (halfWidth - r);
        const double qy = std::abs(y + 0.5 - rect.y - halfHeight) - (halfHeight - r);
        const double distance =
            std::hypot(std::max(0.0, qx), std::max(0.0, qy)) + std::min(std::max(qx, qy), 0.0) - r;
        const double outer = std::clamp(0.5 - distance, 0.0, 1.0);
        const double inner = std::clamp(-0.5 - distance, 0.0, 1.0);
        return static_cast<unsigned>(std::lround((outer - inner) * opacity));
    }

    bool ValidRect(int width, int height, const util::Box& rect, uint8_t opacity) {
        return width > 0 && height > 0 && width <= 65535 && height <= 65535 && rect.width > 0 &&
               rect.height > 0 && opacity != 0;
    }
}  // namespace

void RasterizeOsdRoundedRectI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height,
                                 const util::Box& rect, Color color, uint8_t opacity, int radius) {
    if (!y || !u || !v || !ValidRect(width, height, rect, opacity))
        return;
    if (radius <= 0) {
        RasterizeOsdRectI420(y, u, v, width, height, rect, color, opacity);
        return;
    }
    const int x0 = static_cast<int>(std::clamp<int64_t>(rect.x, 0, width));
    const int y0 = static_cast<int>(std::clamp<int64_t>(rect.y, 0, height));
    const int x1 = static_cast<int>(std::clamp<int64_t>(int64_t(rect.x) + rect.width, 0, width));
    const int y1 = static_cast<int>(std::clamp<int64_t>(int64_t(rect.y) + rect.height, 0, height));
    const auto c = ToYuv(color);
    for (int by = y0 / 2; by < (y1 + 1) / 2; ++by) {
        for (int bx = x0 / 2; bx < (x1 + 1) / 2; ++bx) {
            unsigned sum = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int px = bx * 2 + dx, py = by * 2 + dy;
                    if (px >= width || py >= height)
                        continue;
                    const auto a = RoundedCoverage(rect, radius, px, py, opacity);
                    sum += a;
                    Blend(y[static_cast<size_t>(py) * width + px], c.y, a);
                }
            }
            if (bx < width / 2 && by < height / 2) {
                const size_t index = static_cast<size_t>(by) * (width / 2) + bx;
                const auto a       = (sum + 2U) / 4U;
                Blend(u[index], c.u, a);
                Blend(v[index], c.v, a);
            }
        }
    }
}

void RasterizeOsdRoundedRectRgb(uint8_t* rgb, int width, int height, bool bgr, const util::Box& rect,
                                Color color, uint8_t opacity, int radius) {
    if (!rgb || !ValidRect(width, height, rect, opacity))
        return;
    const int x0       = static_cast<int>(std::clamp<int64_t>(rect.x, 0, width));
    const int y0       = static_cast<int>(std::clamp<int64_t>(rect.y, 0, height));
    const int x1       = static_cast<int>(std::clamp<int64_t>(int64_t(rect.x) + rect.width, 0, width));
    const int y1       = static_cast<int>(std::clamp<int64_t>(int64_t(rect.y) + rect.height, 0, height));
    const uint8_t c[3] = {bgr ? color.blue : color.red, color.green, bgr ? color.red : color.blue};
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const auto a = RoundedCoverage(rect, radius, px, py, opacity);
            for (int channel = 0; channel < 3; ++channel)
                Blend(rgb[(static_cast<size_t>(py) * width + px) * 3 + channel], c[channel], a);
        }
    }
}

void RasterizeOsdRoundedRectOutlineI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height,
                                        const util::Box& rect, Color color, uint8_t opacity, int radius) {
    if (!y || !u || !v || !ValidRect(width, height, rect, opacity))
        return;
    const int x0 = static_cast<int>(std::clamp<int64_t>(rect.x, 0, width));
    const int y0 = static_cast<int>(std::clamp<int64_t>(rect.y, 0, height));
    const int x1 = static_cast<int>(std::clamp<int64_t>(int64_t(rect.x) + rect.width, 0, width));
    const int y1 = static_cast<int>(std::clamp<int64_t>(int64_t(rect.y) + rect.height, 0, height));
    const auto c = ToYuv(color);
    for (int by = y0 / 2; by < (y1 + 1) / 2; ++by) {
        for (int bx = x0 / 2; bx < (x1 + 1) / 2; ++bx) {
            unsigned sum = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int px = bx * 2 + dx, py = by * 2 + dy;
                    if (px >= width || py >= height)
                        continue;
                    const auto a = RoundedOutlineCoverage(rect, radius, px, py, opacity);
                    sum += a;
                    if (a == 0)
                        continue;
                    Blend(y[static_cast<size_t>(py) * width + px], c.y, a);
                }
            }
            if (bx < width / 2 && by < height / 2) {
                const size_t index = static_cast<size_t>(by) * (width / 2) + bx;
                const auto a       = (sum + 2U) / 4U;
                Blend(u[index], c.u, a);
                Blend(v[index], c.v, a);
            }
        }
    }
}

void RasterizeOsdRoundedRectOutlineRgb(uint8_t* rgb, int width, int height, bool bgr, const util::Box& rect,
                                       Color color, uint8_t opacity, int radius) {
    if (!rgb || !ValidRect(width, height, rect, opacity))
        return;
    const int x0       = static_cast<int>(std::clamp<int64_t>(rect.x, 0, width));
    const int y0       = static_cast<int>(std::clamp<int64_t>(rect.y, 0, height));
    const int x1       = static_cast<int>(std::clamp<int64_t>(int64_t(rect.x) + rect.width, 0, width));
    const int y1       = static_cast<int>(std::clamp<int64_t>(int64_t(rect.y) + rect.height, 0, height));
    const uint8_t c[3] = {bgr ? color.blue : color.red, color.green, bgr ? color.red : color.blue};
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const auto a = RoundedOutlineCoverage(rect, radius, px, py, opacity);
            if (a == 0)
                continue;
            for (int channel = 0; channel < 3; ++channel)
                Blend(rgb[(static_cast<size_t>(py) * width + px) * 3 + channel], c[channel], a);
        }
    }
}

void RasterizeOsdTextI420(uint8_t* y, uint8_t* u, uint8_t* v, int width, int height, int x, int top,
                          const IOsdTextRenderer::OutlinedTextBitmap& bitmap, Color color) {
    if (!y || !u || !v || width <= 0 || height <= 0 || width > 65535 || height > 65535 || bitmap.width <= 0 ||
        bitmap.height <= 0 || bitmap.bodyAlpha.size() != static_cast<size_t>(bitmap.width) * bitmap.height ||
        bitmap.outlineAlpha.size() != bitmap.bodyAlpha.size())
        return;
    const int64_t left = int64_t(x) - bitmap.offsetX, upper = int64_t(top) - bitmap.offsetY;
    const int x0 = static_cast<int>(std::clamp<int64_t>(left, 0, width));
    const int y0 = static_cast<int>(std::clamp<int64_t>(upper, 0, height));
    const int x1 = static_cast<int>(std::clamp<int64_t>(left + bitmap.width, 0, width));
    const int y1 = static_cast<int>(std::clamp<int64_t>(upper + bitmap.height, 0, height));
    const auto c = ToYuv(color);
    for (int by = y0 / 2; by < (y1 + 1) / 2; ++by) {
        for (int bx = x0 / 2; bx < (x1 + 1) / 2; ++bx) {
            uint64_t outlineWeight = 0, bodyWeight = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int px = bx * 2 + dx, py = by * 2 + dy;
                    if (px < x0 || px >= x1 || py < y0 || py >= y1)
                        continue;
                    const auto index = static_cast<size_t>(py - upper) * bitmap.width + px - left;
                    const auto oa = bitmap.outlineAlpha[index], ba = bitmap.bodyAlpha[index];
                    // Composite each luma sample before averaging into chroma.
                    // Averaging the two masks separately would let text in one
                    // pixel incorrectly cover the outline of a neighbouring pixel.
                    outlineWeight += unsigned(oa) * (255U - ba);
                    bodyWeight += unsigned(ba) * 255U;
                    auto& pixel = y[static_cast<size_t>(py) * width + px];
                    Blend(pixel, 16, oa);
                    Blend(pixel, c.y, ba);
                }
            }
            if (bx < width / 2 && by < height / 2) {
                const size_t index             = static_cast<size_t>(by) * (width / 2) + bx;
                constexpr uint64_t denominator = 4U * 255U * 255U;
                const auto backgroundWeight    = denominator - outlineWeight - bodyWeight;
                u[index] = static_cast<uint8_t>((backgroundWeight * u[index] + outlineWeight * 128U +
                                                 bodyWeight * c.u + denominator / 2) /
                                                denominator);
                v[index] = static_cast<uint8_t>((backgroundWeight * v[index] + outlineWeight * 128U +
                                                 bodyWeight * c.v + denominator / 2) /
                                                denominator);
            }
        }
    }
}

}  // namespace cosmo::media
