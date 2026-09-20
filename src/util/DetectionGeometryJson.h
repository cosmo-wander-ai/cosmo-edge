#pragma once

#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

#include "util/DetectionGeometry.h"

namespace cosmo::util {

// Additive wire field: absent for ordinary detections and prediction-only tracks.
inline void WriteOrientedCorners(nlohmann::json& object, const std::optional<Quad>& corners) {
    if (!corners) {
        object.erase("orientedCorners");
        return;
    }
    auto points = nlohmann::json::array();
    for (const auto& point : *corners) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            throw std::invalid_argument("orientedCorners must contain finite coordinates");
        points.push_back({{"x", point.x}, {"y", point.y}});
    }
    object["orientedCorners"] = std::move(points);
}

inline std::optional<Quad> ReadOrientedCorners(const nlohmann::json& object) {
    const auto it = object.find("orientedCorners");
    if (it == object.end() || it->is_null())
        return std::nullopt;
    if (!it->is_array() || it->size() != 4)
        throw std::invalid_argument("orientedCorners requires four vertices");
    Quad points;
    for (size_t index = 0; index < points.size(); ++index) {
        const auto& point = it->at(index);
        if (!point.is_object() || !point.contains("x") || !point.contains("y") ||
            !point.at("x").is_number() || !point.at("y").is_number())
            throw std::invalid_argument("orientedCorners coordinates must be numbers");
        points[index] = {point.at("x").get<float>(), point.at("y").get<float>()};
        if (!std::isfinite(points[index].x) || !std::isfinite(points[index].y))
            throw std::invalid_argument("orientedCorners must contain finite coordinates");
    }
    return points;
}

}  // namespace cosmo::util
