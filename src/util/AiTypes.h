// AI Data type definition (Split from infer/AiCommon.h)
// Pure data types, no function declarations, shared by util/flow/infer

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "util/Rect.h"

namespace cosmo {

enum class AITrackingStatus { NEW, TRACKING, LOSS, UNKNOW };

enum class AIMotionState { UNCERTAIN, MOVING, STILL };

enum class AIShapeChangeState { UNCERTAIN, CHANGE, STILL };

enum class AIFilterType {
    None,
    NoRelatedTarget,
    MotionStatus,
    TrackingStatus,
    ShapeStatus,
    TargetCount,
    TargetFilter
};

enum class TargetPosition {
    kBottom = 0,
    kCenter,
    kTop,
};

enum class TargetAreaType {
    kNormal = 0,
    kShield,
};

enum class TargetPositionOnLineType {
    kOnLine = 0,
    kLeft,
    kRight,
    kUnknown,
};

enum class TargetBreakLineType {
    kNone = 0,  // Not crossing the line
    kPos,       // Crossing the line in positive direction
    kNeg        // Crossing the line in negative direction
};

struct TargetAreaUnit {
    TargetPosition position{TargetPosition::kCenter};
    TargetAreaType type{TargetAreaType::kNormal};
    std::string area_id;
    std::string area_name;
    bool is_associated_area{false};  // Whether it is an associated area
    std::string main_area_id;        // If it is an associated area, bring the ID of the main area
};

struct TargetLineUnit {
    TargetPosition position{TargetPosition::kCenter};
    TargetPositionOnLineType type{TargetPositionOnLineType::kUnknown};
    TargetBreakLineType status{TargetBreakLineType::kNone};
    std::string area_id;
    std::string area_name;
    bool is_associated_area{false};  // Whether it is an associated area
    std::string main_area_id;        // If it is an associated area, bring the ID of the main area
};

struct TargetAreaSign {
    std::vector<TargetAreaUnit> areas;
    std::vector<TargetAreaUnit> shielded_areas;
    std::vector<TargetLineUnit> lines;
};

struct AiLabelParam {
    std::string label;
    float confidence{0.0};
    TargetPosition pos{TargetPosition::kBottom};  // Position entering the area
};

struct AiConfidence {
    std::string label;
    std::string atomic_code;
    float confidence{0.0};
};

// Attribute result
struct AiAttribute {
    std::string category;     // Category
    std::string label;        // Label
    std::string atomic_code;  // Code
    float confidence{0.0};    // Label confidence
};

// OCR result (reserved — type declared in AiCommon.h but not yet instantiated)
struct AiOcrValue {
    std::string atomic_code;  // Code
    std::string value;        // Label
    float confidence{0.0};    // Label confidence
};

struct AiLandmarkData {
    std::vector<util::Point> landmark;
};

struct AiFeature {
    std::vector<float> feature;
};

struct AiMask {
    std::vector<uint8_t> data;
    int width  = 0;
    int height = 0;
};

struct AiDetectMatchHighScoreInfo {
    int64_t setPicCount{-1};  // Number of photos in the comparison library
    std::string group_id;     // Matched target library ID
    std::string group_name;   // Matched target library name
    std::string name;         // Matched photo name
    bool matched{false};      // Successfully matched
    std::string match_id;  // Matched target ID (even if not exceeding threshold, put the target with highest
                           // similarity)
    float match_degree{-1.0};    // Similarity degree
    std::string base_image_url;  // Base image URL for comparison
    std::string person_id;       // Personnel ID
    std::string person_code;     // Personnel code
};

enum class FaceAngle {
    FaceAngleFront = 0,  // Front face
    FaceAngleSlanted,    // Slightly slanted face
    FaceAngleFull,       // Other
    FaceAngleMax         // Invalid
};
constexpr bool ValidateFaceAngle(int value) noexcept {
    return value >= static_cast<int>(FaceAngle::FaceAngleFront) &&
           value < static_cast<int>(FaceAngle::FaceAngleMax);
}

enum class GBVehicleColorType {
    kBlack = 1,  // Black
    kWhite,      // White
    kGray,       // Gray / Silver-gray
    kRed,
    kBlue,
    kYellow,
    kOrange,
    kBrown,
    kGreen,
    kPurple,
    kCyan,  // Cyan
    kPink,  // Pink
    kMixColor = 90,
    kMultiCar,
    kColorPoorQuality,
    kOther = 99
};
constexpr bool ValidateGBVehicleColor(int value) noexcept {
    switch (static_cast<GBVehicleColorType>(value)) {
        case GBVehicleColorType::kBlack:
        case GBVehicleColorType::kWhite:
        case GBVehicleColorType::kGray:
        case GBVehicleColorType::kRed:
        case GBVehicleColorType::kBlue:
        case GBVehicleColorType::kYellow:
        case GBVehicleColorType::kOrange:
        case GBVehicleColorType::kBrown:
        case GBVehicleColorType::kGreen:
        case GBVehicleColorType::kPurple:
        case GBVehicleColorType::kCyan:
        case GBVehicleColorType::kPink:
        case GBVehicleColorType::kMixColor:
        case GBVehicleColorType::kMultiCar:
        case GBVehicleColorType::kColorPoorQuality:
        case GBVehicleColorType::kOther:
            return true;
        default:
            return false;
    }
}

enum class GBVehicleDirectionType { kFront = 1, kBack, kSide, kPoorQuality = 9 };
constexpr bool ValidateGBVehicleDirection(int value) noexcept {
    switch (static_cast<GBVehicleDirectionType>(value)) {
        case GBVehicleDirectionType::kFront:
        case GBVehicleDirectionType::kBack:
        case GBVehicleDirectionType::kSide:
        case GBVehicleDirectionType::kPoorQuality:
            return true;
        default:
            return false;
    }
}

}  // namespace cosmo
