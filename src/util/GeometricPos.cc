// Bounding-box geometry helpers — implementation.

#include "util/GeometricPos.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "util/GeometricCalculation.h"
#include "util/Log.h"

namespace cosmo::util {

// Import cosmo types used by this module.
using cosmo::MsgPoint;
using cosmo::TargetPosition;
using cosmo::TargetPositionOnLineType;

cosmo::util::Point GetPointFromRect(cosmo::util::Box rect, TargetPosition pos) {
    cosmo::util::Point point;
    if (TargetPosition::kTop == pos) {
        point.x = rect.x + rect.width / 2;
        point.y = rect.y;
    } else if (TargetPosition::kBottom == pos) {
        point.x = rect.x + rect.width / 2;
        point.y = rect.y + rect.height;
    } else {
        point.x = rect.x + rect.width / 2;
        point.y = rect.y + rect.height / 2;
    }
    return point;
}

namespace {

    /// Convert a pixel-space point to normalized coordinates [0.0, 1.0].
    MsgPoint PointToMsgPoint(const cosmo::util::Point& point, int pic_width, int pic_height) {
        MsgPoint msg_point;
        if (pic_width <= 0 || pic_height <= 0) {
            LOG_WARN("invalid frame dimensions {}x{}", pic_width, pic_height);
            return msg_point;
        }
        msg_point.x = std::clamp(static_cast<double>(point.x) / pic_width, 0.0, 1.0);
        msg_point.y = std::clamp(static_cast<double>(point.y) / pic_height, 0.0, 1.0);
        return msg_point;
    }

}  // namespace

MsgPoint GetMsgPointFromRect(cosmo::util::Box rect, TargetPosition pos, cosmo::util::Point& point,
                             int pic_width, int pic_height) {
    point = GetPointFromRect(rect, pos);
    return PointToMsgPoint(point, pic_width, pic_height);
}

bool BoxInArea(cosmo::util::Box rect, TargetPosition pos, cosmo::util::Point& point,
               const std::vector<MsgPoint>& area_points, int pic_width, int pic_height) {
    if (area_points.size() < 3) {
        return false;
    }
    auto msg_point = GetMsgPointFromRect(rect, pos, point, pic_width, pic_height);
    return (PointRelativePolygon(msg_point, area_points.data(), area_points.size()) < 0);
}

TargetPositionOnLineType BoxOnLinePos(cosmo::util::Box rect, TargetPosition pos, cosmo::util::Point& point,
                                      const std::vector<MsgPoint>& area_points, int pic_width,
                                      int pic_height) {
    if (area_points.size() < 2) {
        return TargetPositionOnLineType::kUnknown;
    }

    auto msg_point = GetMsgPointFromRect(rect, pos, point, pic_width, pic_height);

    auto point_sign = [&area_points](auto& pt) {
        // Find the nearest line segment.
        size_t nearest  = 0;
        double distance = std::numeric_limits<double>::max();
        for (size_t i = 1; i < area_points.size(); ++i) {
            auto dist = PointLineDistance(pt, area_points[i - 1], area_points[i]);
            if (dist < distance) {
                nearest  = i;
                distance = dist;
            }
        }
        if (nearest != 0) {
            // Swap point order because the screen coordinate system is mirrored.
            return PointRelativeLine(pt, area_points[nearest], area_points[nearest - 1]);
        }
        return 0.0;
    };

    auto value = point_sign(msg_point);
    if (value < 0) {
        return TargetPositionOnLineType::kLeft;
    } else if (value > 0) {
        return TargetPositionOnLineType::kRight;
    } else {
        return TargetPositionOnLineType::kOnLine;
    }
}

cosmo::util::Box BoxMerge(cosmo::util::Box box1, cosmo::util::Box box2) {
    cosmo::util::Box box;
    box.x = std::min(box1.x, box2.x);
    box.y = std::min(box1.y, box2.y);

    auto right_bottom_x = std::max(box1.x + box1.width, box2.x + box2.width);
    auto right_bottom_y = std::max(box1.y + box1.height, box2.y + box2.height);

    box.width  = right_bottom_x - box.x;
    box.height = right_bottom_y - box.y;
    return box;
}

namespace {

    double PointDistanceNorm(cosmo::util::Point point1, cosmo::util::Point point2, int pic_width,
                             int pic_height) {
        if (pic_width <= 0 || pic_height <= 0) {
            return std::sqrt(std::pow(point2.x - point1.x, 2) + std::pow(point2.y - point1.y, 2));
        }
        double x_dist = static_cast<double>(point2.x - point1.x) / pic_width;
        double y_dist = static_cast<double>(point2.y - point1.y) / pic_height;
        // After normalization each axis has a max distance of 1; the diagonal max is sqrt(2).
        return std::sqrt(x_dist * x_dist + y_dist * y_dist) / std::sqrt(2.0);
    }

}  // namespace

double BoxDistance(cosmo::util::Box box1, cosmo::util::Box box2, int pic_width, int pic_height) {
    auto point1 = GetPointFromRect(box1, TargetPosition::kCenter);
    auto point2 = GetPointFromRect(box2, TargetPosition::kCenter);
    return PointDistanceNorm(point1, point2, pic_width, pic_height);
}

namespace {

    bool Intersect(cosmo::util::Box box1, cosmo::util::Box box2) {
        int left   = std::max(box1.x, box2.x);
        int top    = std::max(box1.y, box2.y);
        int right  = std::min(box1.x + box1.width, box2.x + box2.width);
        int bottom = std::min(box1.y + box1.height, box2.y + box2.height);
        return left < right && top < bottom;
    }

    int IntersectionArea(cosmo::util::Box box1, cosmo::util::Box box2) {
        if (!Intersect(box1, box2)) {
            return 0;
        }
        int left   = std::max(box1.x, box2.x);
        int top    = std::max(box1.y, box2.y);
        int right  = std::min(box1.x + box1.width, box2.x + box2.width);
        int bottom = std::min(box1.y + box1.height, box2.y + box2.height);
        return (right - left) * (bottom - top);
    }

    int GetScaleLen(int len, float scale_param) {
        return static_cast<int>((scale_param - 1.0f) * static_cast<float>(len));
    }

}  // namespace

float IntersectionUnionRatio(cosmo::util::Box box1, cosmo::util::Box box2) {
    float iou     = 0.0f;
    auto square   = IntersectionArea(box1, box2);
    auto product1 = box2.height * box2.width;
    auto product2 = box1.width * box1.height;
    auto product  = product1 + product2 - square;
    if (product > 0) {
        iou = static_cast<float>(square) / static_cast<float>(product);
    }
    return iou;
}

float IntersectionIncludeRatio(cosmo::util::Box box1, cosmo::util::Box box2) {
    float ratio  = 0.0f;
    auto square  = IntersectionArea(box1, box2);
    auto product = box2.height * box2.width;
    if (product > 0) {
        ratio = static_cast<float>(square) / static_cast<float>(product);
    }
    return ratio;
}

cosmo::util::Box DoScaleBox(cosmo::util::Box input_box, TargetScalerParam param, int pic_width,
                            int pic_height) {
    int x_move  = 0;
    int y_move  = 0;
    int w_scale = 0;
    int h_scale = 0;

    if (input_box.width <= 0 || input_box.height <= 0) {
        return input_box;
    }

    // Scale north (upward).
    if (FloatDiff(param.scale_north, 1.0)) {
        auto scale_len = GetScaleLen(input_box.height, param.scale_north);
        y_move -= scale_len;
        h_scale += scale_len;
    }
    // Scale south (downward).
    if (FloatDiff(param.scale_south, 1.0)) {
        auto scale_len = GetScaleLen(input_box.height, param.scale_south);
        h_scale += scale_len;
    }
    // Scale east (rightward).
    if (FloatDiff(param.scale_east, 1.0)) {
        auto scale_len = GetScaleLen(input_box.width, param.scale_east);
        w_scale += scale_len;
    }
    // Scale west (leftward).
    if (FloatDiff(param.scale_west, 1.0)) {
        auto scale_len = GetScaleLen(input_box.width, param.scale_west);
        x_move -= scale_len;
        w_scale += scale_len;
    }
    // Horizontal (left + right symmetrically).
    if (FloatDiff(param.scale_horizontal, 1.0)) {
        auto scale_len = GetScaleLen(input_box.width, param.scale_horizontal);
        x_move -= scale_len / 2;
        w_scale += scale_len;
    }
    // Vertical (up + down symmetrically).
    if (FloatDiff(param.scale_longitudinal, 1.0)) {
        auto scale_len = GetScaleLen(input_box.height, param.scale_longitudinal);
        y_move -= scale_len / 2;
        h_scale += scale_len;
    }
    // Uniform (all directions).
    if (FloatDiff(param.scale_side, 1.0)) {
        auto scale_len_w = GetScaleLen(input_box.width, param.scale_side);
        auto scale_len_h = GetScaleLen(input_box.height, param.scale_side);
        x_move -= scale_len_w / 2;
        w_scale += scale_len_w;
        y_move -= scale_len_h / 2;
        h_scale += scale_len_h;
    }

    cosmo::util::Box out;
    out.x      = input_box.x + x_move;
    out.y      = input_box.y + y_move;
    out.width  = input_box.width + w_scale;
    out.height = input_box.height + h_scale;

    // Clamp to frame bounds.
    if (out.x < 0) {
        out.width = out.width + out.x;  // shrink width by the overshoot
        out.x     = 0;
    }
    if (out.y < 0) {
        out.height = out.height + out.y;  // shrink height by the overshoot  [BUG FIX: was out.x]
        out.y      = 0;
    }
    if (out.x > pic_width) {
        out.x = pic_width;
    }
    if (out.y > pic_height) {
        out.y = pic_height;
    }
    if (out.x + out.width > pic_width) {
        out.width = (pic_width > out.x) ? (pic_width - out.x) : 0;
    }
    if (out.y + out.height > pic_height) {
        out.height = (pic_height > out.y) ? (pic_height - out.y) : 0;
    }
    return out;
}

bool BoxIncludeBox(cosmo::util::Box src, cosmo::util::Box target) {
    return (target.x >= src.x) && (target.x + target.width <= src.x + src.width) && (target.y >= src.y) &&
           (target.y + target.height <= src.y + src.height);
}

std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> GetBoxOsdLines(cosmo::util::Box box, int width,
                                                                              int height) {
    std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines;
    box.x      = std::clamp(box.x, 0, width);
    box.y      = std::clamp(box.y, 0, height);
    box.width  = std::clamp(box.width, 0, width);
    box.height = std::clamp(box.height, 0, height);
    if (box.x + box.width > width) {
        box.width = width - box.x;
    }
    if (box.y + box.height > height) {
        box.height = height - box.y;
    }

    std::pair<cosmo::util::Point, cosmo::util::Point> line;
    // Top edge.
    line.first.x  = box.x;
    line.first.y  = box.y;
    line.second.x = box.x + box.width;
    line.second.y = box.y;
    lines.push_back(line);
    // Right edge.
    line.first.x  = box.x + box.width;
    line.first.y  = box.y;
    line.second.x = box.x + box.width;
    line.second.y = box.y + box.height;
    lines.push_back(line);
    // Bottom edge.
    line.first.x  = box.x + box.width;
    line.first.y  = box.y + box.height;
    line.second.x = box.x;
    line.second.y = box.y + box.height;
    lines.push_back(line);
    // Left edge.
    line.first.x  = box.x;
    line.first.y  = box.y + box.height;
    line.second.x = box.x;
    line.second.y = box.y;
    lines.push_back(line);

    return lines;
}

}  // namespace cosmo::util