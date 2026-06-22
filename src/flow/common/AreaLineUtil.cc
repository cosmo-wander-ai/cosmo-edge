// Convert detection area polygons to drawable line segments.

#include "flow/common/AreaLineUtil.h"

namespace cosmo {

std::vector<std::pair<util::Point, util::Point>> GetAreaLines(const MsgTaskArea& area, int width,
                                                              int height) {
    std::vector<std::pair<util::Point, util::Point>> lines;

    // Recursively collect lines from associated (child) areas
    for (const auto& assArea : area.associatedAreas) {
        auto assLines = GetAreaLines(assArea, width, height);
        if (!assLines.empty())
            lines.insert(lines.end(), assLines.begin(), assLines.end());
    }

    // Convert polygon points to pixel-space line segments
    if (!area.points.empty()) {
        for (size_t index = 1; index < area.points.size(); index++) {
            std::pair<util::Point, util::Point> line;
            line.first.x  = static_cast<int>(area.points[index - 1].x * width);
            line.first.y  = static_cast<int>(area.points[index - 1].y * height);
            line.second.x = static_cast<int>(area.points[index].x * width);
            line.second.y = static_cast<int>(area.points[index].y * height);
            lines.push_back(line);
        }
        // Close the polygon
        if (area.points.size() > 2) {
            std::pair<util::Point, util::Point> line;
            line.first.x  = static_cast<int>(area.points[area.points.size() - 1].x * width);
            line.first.y  = static_cast<int>(area.points[area.points.size() - 1].y * height);
            line.second.x = static_cast<int>(area.points[0].x * width);
            line.second.y = static_cast<int>(area.points[0].y * height);
            lines.push_back(line);
        }
    }

    // Convert line-crossing detection points with direction arrows
    if (!area.linePoints.empty()) {
        auto pointTrans = [width, height](const MsgPoint& origin) {
            return util::Point{static_cast<int>(origin.x * width), static_cast<int>(origin.y * height)};
        };

        size_t drawDirectionIndex = area.linePoints.size() / 2;
        for (size_t i = 1; i < area.linePoints.size(); ++i) {
            auto lastPoint = pointTrans(area.linePoints[i - 1]);
            auto currPoint = pointTrans(area.linePoints[i]);

            auto lineDir    = currPoint - lastPoint;
            auto lineLength = util::Length(lineDir);
            if (lineLength == 0) {
                continue;
            }

            std::pair<util::Point, util::Point> line;
            line.first  = lastPoint;
            line.second = currPoint;
            lines.push_back(line);

            // Draw direction indicator only at the midpoint segment
            if (drawDirectionIndex != i)
                continue;

            // Perpendicular indicator
            auto centPoint  = (lastPoint + currPoint) / 2;
            auto perpLength = height * 0.04;
            auto perpDir    = util::LineDirection(util::Perpendicular(lineDir), perpLength);
            auto finaPoint  = centPoint + perpDir;
            if (area.iderectionType == DirectionType::DirectionTypeOneWay) {
                lines.push_back(std::make_pair(centPoint, finaPoint));
            }

            // Arrow head
            auto centPerpPoint = (centPoint + finaPoint) / 2;
            auto arrowDir      = util::LineDirection(lineDir, perpLength / 2);
            lines.push_back(std::make_pair(finaPoint, centPerpPoint + arrowDir));
            lines.push_back(std::make_pair(finaPoint, centPerpPoint + arrowDir * -1));

            // Two-way: draw negative direction
            if (area.iderectionType == DirectionType::DirectionTypeTwoWay) {
                auto negativeLineDir = lastPoint - currPoint;
                auto negativePerpDir = util::LineDirection(util::Perpendicular(negativeLineDir), perpLength);
                auto negativeFinaPoint = centPoint + negativePerpDir;
                lines.push_back(std::make_pair(finaPoint, negativeFinaPoint));

                auto negativeCentPerpPoint = (centPoint + negativeFinaPoint) / 2;
                auto negativeArrowDir      = util::LineDirection(negativeLineDir, perpLength / 2);
                lines.push_back(std::make_pair(negativeFinaPoint, negativeCentPerpPoint + negativeArrowDir));
                lines.push_back(
                    std::make_pair(negativeFinaPoint, negativeCentPerpPoint + negativeArrowDir * -1));
            }
        }
    }

    return lines;
}

}  // namespace cosmo
