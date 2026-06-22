// Bounding-box geometry helpers — area/line tests, scaling, IoU.

#pragma once

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"

namespace cosmo::util {

/// Extract a representative point (top-center, bottom-center, or center) from a box.
[[nodiscard]] cosmo::util::Point GetPointFromRect(cosmo::util::Box rect, cosmo::TargetPosition pos);

/// Convert a box + position to a normalized MsgPoint (0.0–1.0).
/// Also writes the pixel-space point to @p point.
[[nodiscard]] cosmo::MsgPoint GetMsgPointFromRect(cosmo::util::Box rect, cosmo::TargetPosition pos,
                                                  cosmo::util::Point& point, int pic_width = 1920,
                                                  int pic_height = 1080);

/// Test whether the representative point of @p rect falls inside the polygon
/// defined by @p area_points.
[[nodiscard]] bool BoxInArea(cosmo::util::Box rect, cosmo::TargetPosition pos, cosmo::util::Point& point,
                             const std::vector<cosmo::MsgPoint>& area_points, int pic_width = 1920,
                             int pic_height = 1080);

/// Determine on which side of a poly-line the representative point of @p rect lies.
[[nodiscard]] cosmo::TargetPositionOnLineType BoxOnLinePos(cosmo::util::Box rect, cosmo::TargetPosition pos,
                                                           cosmo::util::Point& point,
                                                           const std::vector<cosmo::MsgPoint>& area_points,
                                                           int pic_width = 1920, int pic_height = 1080);

/// Merge two boxes into the smallest enclosing box.
[[nodiscard]] cosmo::util::Box BoxMerge(cosmo::util::Box rect1, cosmo::util::Box rect2);

/// Distance between box centers.
/// When pic_width and pic_height are > 0, returns a normalized distance in [0, 1];
/// otherwise returns the absolute pixel distance.
[[nodiscard]] double BoxDistance(cosmo::util::Box box1, cosmo::util::Box box2, int pic_width = 0,
                                 int pic_height = 0);

/// Intersection-over-Union ratio (IoU).
[[nodiscard]] float IntersectionUnionRatio(cosmo::util::Box box1, cosmo::util::Box box2);

/// Fraction of box2 that is covered by box1.
[[nodiscard]] float IntersectionIncludeRatio(cosmo::util::Box box1, cosmo::util::Box box2);

/// Parameters for directional box scaling.
struct TargetScalerParam {
    float scale_side{1.0f};
    float scale_horizontal{1.0f};
    float scale_longitudinal{1.0f};
    float scale_north{1.0f};
    float scale_south{1.0f};
    float scale_east{1.0f};
    float scale_west{1.0f};
};

/// Scale @p input_box according to @p param, clamped to [0, pic_width) × [0, pic_height).
[[nodiscard]] cosmo::util::Box DoScaleBox(cosmo::util::Box input_box, TargetScalerParam param,
                                          int pic_width = 1920, int pic_height = 1080);

/// Test whether @p target is fully contained within @p src.
[[nodiscard]] bool BoxIncludeBox(cosmo::util::Box src, cosmo::util::Box target);

/// Get the four edges of a box as line-segment pairs, clamped to frame bounds.
[[nodiscard]] std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> GetBoxOsdLines(
    cosmo::util::Box box, int width, int height);

}  // namespace cosmo::util

// ---------------------------------------------------------------------------
// Backward compatibility — keep legacy cosmo:: names available during migration.
// ---------------------------------------------------------------------------
namespace cosmo {
using util::BoxDistance;
using util::BoxInArea;
using util::BoxIncludeBox;
using util::BoxMerge;
using util::BoxOnLinePos;
using util::DoScaleBox;
using util::GetBoxOsdLines;
using util::GetMsgPointFromRect;
using util::GetPointFromRect;
using util::IntersectionIncludeRatio;
using util::IntersectionUnionRatio;
using util::TargetScalerParam;
}  // namespace cosmo
