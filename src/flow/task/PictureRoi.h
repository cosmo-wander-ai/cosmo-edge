#pragma once

#include <algorithm>
#include <cmath>
#include <set>

#include "flow/common/AlgDataUnit.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo::picture_roi {

inline double Cross(const MsgPoint& a, const MsgPoint& b, const MsgPoint& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

inline bool OnSegment(const MsgPoint& a, const MsgPoint& b, const MsgPoint& p) {
    return std::abs(Cross(a, b, p)) <= 1e-12 && p.x >= std::min(a.x, b.x) - 1e-12 &&
           p.x <= std::max(a.x, b.x) + 1e-12 && p.y >= std::min(a.y, b.y) - 1e-12 &&
           p.y <= std::max(a.y, b.y) + 1e-12;
}

inline bool Intersects(const MsgPoint& a, const MsgPoint& b, const MsgPoint& c, const MsgPoint& d) {
    return OnSegment(a, b, c) || OnSegment(a, b, d) || OnSegment(c, d, a) || OnSegment(c, d, b) ||
           ((Cross(a, b, c) > 0) != (Cross(a, b, d) > 0) && (Cross(c, d, a) > 0) != (Cross(c, d, b) > 0));
}

// Image ROIs are simple normalized polygons, not video tripwires or associated areas.
inline bool Valid(const std::vector<MsgTaskArea>& areas) {
    if (areas.size() > 100)
        return false;
    std::set<std::string> ids;
    for (const auto& area : areas) {
        const auto& p = area.points;
        if (area.areaId.empty() || !ids.insert(area.areaId).second || p.size() < 3 || p.size() > 256 ||
            !area.linePoints.empty() || !area.associatedAreas.empty())
            return false;
        double twiceArea = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            const auto& a = p[i];
            const auto& b = p[(i + 1) % p.size()];
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || a.x < 0 || a.x > 1 || a.y < 0 || a.y > 1 ||
                (a.x == b.x && a.y == b.y))
                return false;
            twiceArea += a.x * b.y - b.x * a.y;
            for (size_t j = i + 1; j < p.size(); ++j) {
                if (j == i + 1 || (i == 0 && j + 1 == p.size()))
                    continue;
                if (Intersects(a, b, p[j], p[(j + 1) % p.size()]))
                    return false;
            }
        }
        if (std::abs(twiceArea) <= 1e-12)
            return false;
    }
    return true;
}

inline bool Contains(const MsgTaskArea& area, const MsgPoint& p) {
    bool inside = false;
    for (size_t i = 0, j = area.points.size() - 1; i < area.points.size(); j = i++) {
        const auto& a = area.points[j];
        const auto& b = area.points[i];
        if (OnSegment(a, b, p))
            return true;
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

// Preserve original image/box coordinates. Filter before downstream classification and rules.
inline bool Apply(const AlgDataPtr& data, const MsgTaskConfig& config) {
    if (config.areas.empty() && config.shieldedAreas.empty())
        return true;
    auto result = data->chanDataDetect.detRet;
    if (!result || result->targets.empty())
        return true;
    const double width  = data->chanDataDec.frame ? data->chanDataDec.frame->GetWidth() : result->picWidth;
    const double height = data->chanDataDec.frame ? data->chanDataDec.frame->GetHeight() : result->picHeight;
    if (width <= 0 || height <= 0)
        return false;
    for (auto& target : result->targets) {
        if (target.bFilter)
            continue;
        const auto& box = target.box;
        MsgPoint center;
        center.x = (static_cast<double>(box.x) + box.width / 2.0) / width;
        center.y = (static_cast<double>(box.y) + box.height / 2.0) / height;
        target.areaSign.areas.clear();
        bool included = config.areas.empty();
        for (const auto& area : config.areas) {
            if (!Contains(area, center))
                continue;
            included = true;
            TargetAreaUnit member;
            member.area_id   = area.areaId;
            member.area_name = area.name;
            target.areaSign.areas.push_back(std::move(member));
        }
        const bool shielded = std::any_of(config.shieldedAreas.begin(), config.shieldedAreas.end(),
                                          [&](const auto& area) { return Contains(area, center); });
        const bool invalid  = box.width <= 0 || box.height <= 0 || !std::isfinite(center.x) ||
                             !std::isfinite(center.y) || center.x < 0 || center.x > 1 || center.y < 0 ||
                             center.y > 1;
        if (invalid || shielded || !included) {
            target.bFilter    = true;
            target.filterDesc = invalid ? "invalid_roi_target" : (shielded ? "shielded_roi" : "outside_roi");
        }
    }
    return true;
}

}  // namespace cosmo::picture_roi
