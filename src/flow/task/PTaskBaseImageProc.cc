// PTaskBaseImageProc.cc — Pure image processing and geometry algorithms for PTaskBase.
// Split from PTaskBaseUpload.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <cstdint>
#include <vector>

#include "flow/task/PTaskBase.h"
#include "util/Point.h"

namespace cosmo {

std::vector<MsgPoint> ComputeMaskPolygon(const AiMask& mask) {
    std::vector<MsgPoint> polygon;
    if (mask.width <= 0 || mask.height <= 0 || mask.data.empty()) {
        return polygon;
    }

    int w = mask.width;
    int h = mask.height;
    std::vector<util::Point> boundary_pts;
    int step = std::max(1, w / 100);  // Sampling interval
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            if (mask.data[static_cast<size_t>(y * w + x)] > 0) {
                bool edge = false;
                if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                    edge = true;
                } else if (mask.data[static_cast<size_t>((y - 1) * w + x)] == 0 ||
                           mask.data[static_cast<size_t>((y + 1) * w + x)] == 0 ||
                           mask.data[static_cast<size_t>(y * w + x - 1)] == 0 ||
                           mask.data[static_cast<size_t>(y * w + x + 1)] == 0) {
                    edge = true;
                }
                if (edge) {
                    util::Point pt;
                    pt.x = x;
                    pt.y = y;
                    boundary_pts.push_back(pt);
                }
            }
        }
    }

    // Compute convex hull (Monotone Chain)
    if (!boundary_pts.empty()) {
        std::sort(boundary_pts.begin(), boundary_pts.end(), [](const util::Point& a, const util::Point& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        auto cross = [](const util::Point& o, const util::Point& a, const util::Point& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<util::Point> hull;
        for (const auto& p : boundary_pts) {
            while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0)
                hull.pop_back();
            hull.push_back(p);
        }
        size_t t = hull.size() + 1;
        for (int i = static_cast<int>(boundary_pts.size()) - 2; i >= 0; i--) {
            while (hull.size() >= t &&
                   cross(hull[hull.size() - 2], hull.back(), boundary_pts[static_cast<size_t>(i)]) <= 0)
                hull.pop_back();
            hull.push_back(boundary_pts[static_cast<size_t>(i)]);
        }
        if (hull.size() > 1)
            hull.pop_back();

        polygon.reserve(hull.size());
        for (auto& p : hull) {
            MsgPoint mp;
            mp.x = static_cast<double>(p.x) / static_cast<double>(w);
            mp.y = static_cast<double>(p.y) / static_cast<double>(h);
            polygon.push_back(mp);
        }
    }

    return polygon;
}

bool ApplyYuvMask(uint8_t* yuvData, int imgW, int imgH, const AiMask& mask) {
    if (mask.width != imgW || mask.height != imgH || mask.data.empty()) {
        return false;
    }

    // Select a fixed semi-transparent overlay color: semi-transparent green
    // Green (R=0, G=255, B=0) converted to YUV approximately: Y=150, U=44, V=21
    constexpr float alpha     = 0.4f;
    constexpr uint8_t green_Y = 150;
    constexpr uint8_t green_U = 44;
    constexpr uint8_t green_V = 21;

    bool hasMaskApplied = false;

    // 1. Blend Y component (full resolution)
    for (int y = 0; y < imgH; y++) {
        for (int x = 0; x < imgW; x++) {
            int idx = y * imgW + x;
            if (mask.data[static_cast<size_t>(idx)] > 0) {
                yuvData[idx]   = static_cast<uint8_t>(yuvData[idx] * (1.0f - alpha) + green_Y * alpha);
                hasMaskApplied = true;
            }
        }
    }

    // 2. Blend U and V components (half resolution in each dimension)
    for (int y = 0; y < imgH - 1; y += 2) {
        for (int x = 0; x < imgW - 1; x += 2) {
            if (mask.data[static_cast<size_t>(y * imgW + x)] > 0 ||
                mask.data[static_cast<size_t>(y * imgW + x + 1)] > 0 ||
                mask.data[static_cast<size_t>((y + 1) * imgW + x)] > 0 ||
                mask.data[static_cast<size_t>((y + 1) * imgW + x + 1)] > 0) {
                int uv_x  = x / 2;
                int uv_y  = y / 2;
                int u_idx = imgW * imgH + uv_y * (imgW / 2) + uv_x;
                int v_idx = imgW * imgH + (imgW * imgH) / 4 + uv_y * (imgW / 2) + uv_x;

                yuvData[u_idx] = static_cast<uint8_t>(yuvData[u_idx] * (1.0f - alpha) + green_U * alpha);
                yuvData[v_idx] = static_cast<uint8_t>(yuvData[v_idx] * (1.0f - alpha) + green_V * alpha);
            }
        }
    }

    return hasMaskApplied;
}

bool ApplyBgrMask(uint8_t* bgrData, int imgW, int imgH, const AiMask& mask) {
    if (mask.width != imgW || mask.height != imgH || mask.data.empty()) {
        return false;
    }

    // Same semi-transparent green overlay as ApplyYuvMask for visual consistency
    // Green in BGR order: B=0, G=255, R=0
    constexpr float alpha     = 0.4f;
    constexpr uint8_t green_B = 0;
    constexpr uint8_t green_G = 255;
    constexpr uint8_t green_R = 0;

    bool hasMaskApplied = false;

    for (int y = 0; y < imgH; y++) {
        for (int x = 0; x < imgW; x++) {
            if (mask.data[static_cast<size_t>(y * imgW + x)] > 0) {
                int idx          = (y * imgW + x) * 3;
                bgrData[idx]     = static_cast<uint8_t>(bgrData[idx] * (1.0f - alpha) + green_B * alpha);
                bgrData[idx + 1] = static_cast<uint8_t>(bgrData[idx + 1] * (1.0f - alpha) + green_G * alpha);
                bgrData[idx + 2] = static_cast<uint8_t>(bgrData[idx + 2] * (1.0f - alpha) + green_R * alpha);
                hasMaskApplied   = true;
            }
        }
    }

    return hasMaskApplied;
}

}  // namespace cosmo
