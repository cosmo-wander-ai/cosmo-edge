// Convert detection area polygons to drawable line segments.

#pragma once

#include <utility>
#include <vector>

#include "util/Point.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

// Convert area polygons (normalized coords) to pixel-space line segments,
// including direction arrows for one-way / two-way lines.
std::vector<std::pair<util::Point, util::Point>> GetAreaLines(const MsgTaskArea& area, int width, int height);

}  // namespace cosmo
