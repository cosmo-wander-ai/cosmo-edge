// Task parameter utility — area-to-box conversion and direction type parsing.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "util/Keys.h"
#include "util/SafeParse.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
struct TaskBaseArea {
    std::string taskId;
    std::vector<MsgTaskArea> areas;
    std::vector<MsgTaskArea> shieldedAreas;
};

// Parse direction type from dynamic key-value parameters
inline DirectionType GetDirectionTypeFromMsg(const std::vector<MsgDynamicKeyValue>& params) {
    auto it = std::find_if(params.begin(), params.end(),
                           [](const auto& param) { return key::DIRECTION_TYPE == param.key.ToString(); });
    if (it != params.end()) {
        return static_cast<DirectionType>(util::ParseInt(it->value));
    }
    return DirectionType::DirectionTypeOneWay;
}

// Convert area polygon points to local bounding box
inline void AreaToLocalBox(MsgTaskArea& area) {
    double minX = 1.0;
    double minY = 1.0;
    double maxX = 0.0;
    double maxY = 0.0;
    for (auto& point : area.points) {
        if (point.x < minX) {
            minX = point.x;
        }
        if (point.x > maxX) {
            maxX = point.x;
        }
        if (point.y < minY) {
            minY = point.y;
        }
        if (point.y > maxY) {
            maxY = point.y;
        }
    }
    if ((minX < 0.0) || (minX > 1.0)) {
        minX = 0.0;
    }
    if ((minY < 0.0) || (minY > 1.0)) {
        minY = 0.0;
    }
    if ((maxX < 0.0) || (maxX > 1.0)) {
        maxX = 0.0;
    }
    if ((maxY < 0.0) || (maxY > 1.0)) {
        maxY = 0.0;
    }
    if (minX > maxX) {
        maxX = minX;
    }
    if (minY > maxY) {
        maxY = minY;
    }
    area.pointBox.x      = minX;
    area.pointBox.y      = minY;
    area.pointBox.width  = maxX - minX;
    area.pointBox.height = maxY - minY;
}

// Convert associated area parameters to local representation
inline void AssoAreaToLocal(MsgTaskArea& area) {
    area.iderectionType = GetDirectionTypeFromMsg(area.params);
    AreaToLocalBox(area);
    for (auto& assoArea : area.associatedAreas) {
        if ((!assoArea.points.empty()) || (!assoArea.linePoints.empty())) {
            area.bHaveAssoArea = true;
        }
        AssoAreaToLocal(assoArea);
    }
}

// Convert area parameters to local representation
inline void AreaToLocal(MsgTaskConfig& param) {
    for (auto& area : param.areas) {
        AssoAreaToLocal(area);
    }
}

}  // namespace cosmo
