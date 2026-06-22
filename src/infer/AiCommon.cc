// AiCommon — Common AI detection/tracking/classification data types.

#include "infer/AiCommon.h"

#include <codecvt>
#include <locale>
#include <regex>
#include <unordered_map>

#include "util/InferConstants.h"
#include "util/Log.h"

namespace cosmo {
constexpr std::string_view kLabelFrontFace   = "frontFace";
constexpr std::string_view kLabelSlantedFace = "slantedFace";
constexpr std::string_view kLabelFullFace    = "fullFace";

/*
frontFace:0.0027254522
slantedFace:0.9930965
fullFace:0.00018012524
*/
bool GetFaceAngle(const std::vector<AiConfidence>& classifyRst, AiConfidence& faceAngle) {
    bool is_found        = false;
    float max_confidence = -1.0;
    size_t index         = 0;
    for (index = 0; index < classifyRst.size(); index++) {
        if ((classifyRst[index].label == kLabelFrontFace) ||
            (classifyRst[index].label == kLabelSlantedFace) || (classifyRst[index].label == kLabelFullFace)) {
            if (classifyRst[index].confidence > max_confidence) {
                faceAngle      = classifyRst[index];
                max_confidence = classifyRst[index].confidence;
                is_found       = true;
            }
        }
    }
    return is_found;
}

bool GetFaceAngleType(const AiConfidence& faceAngle, FaceAngle& angle) {
    if (faceAngle.label == kLabelFrontFace) {
        angle = FaceAngle::FaceAngleFront;
        return true;
    }

    if (faceAngle.label == kLabelSlantedFace) {
        angle = FaceAngle::FaceAngleSlanted;
        return true;
    }

    if (faceAngle.label == kLabelFullFace) {
        angle = FaceAngle::FaceAngleFull;
        return true;
    }

    return false;
}

constexpr std::string_view kLabelFaceBlur0      = "face_blur0";
constexpr std::string_view kLabelFaceBlur1      = "face_blur1";
constexpr std::string_view kLabelFaceBlur2      = "face_blur2";
constexpr std::string_view kLabelFaceBlur0Camel = "faceBlur0";
constexpr std::string_view kLabelFaceBlur1Camel = "faceBlur1";
constexpr std::string_view kLabelFaceBlur2Camel = "faceBlur2";
/*
face_blur0:2.1755695e-06
face_blur1:8.9883804e-05
face_blur2:0.99991703
*/
bool GetFaceQuality(const std::vector<AiConfidence>& classifyRst, AiConfidence& faceAngle) {
    AiConfidence max_conf_info;
    AiConfidence face_blur0;
    AiConfidence face_blur1;
    AiConfidence face_blur2;
    size_t find_count    = 0;
    float max_confidence = -1.0;
    size_t index         = 0;
    for (index = 0; index < classifyRst.size(); index++) {
        if ((classifyRst[index].label == kLabelFaceBlur0) ||
            (classifyRst[index].label == kLabelFaceBlur0Camel)) {
            if (classifyRst[index].confidence > max_confidence) {
                max_conf_info  = classifyRst[index];
                max_confidence = classifyRst[index].confidence;
            }
            face_blur0 = classifyRst[index];
            find_count += 1;
        } else if ((classifyRst[index].label == kLabelFaceBlur1) ||
                   (classifyRst[index].label == kLabelFaceBlur1Camel)) {
            if (classifyRst[index].confidence > max_confidence) {
                max_conf_info  = classifyRst[index];
                max_confidence = classifyRst[index].confidence;
            }
            face_blur1 = classifyRst[index];
            find_count += 1;
        } else if ((classifyRst[index].label == kLabelFaceBlur2) ||
                   (classifyRst[index].label == kLabelFaceBlur2Camel)) {
            if (classifyRst[index].confidence > max_confidence) {
                max_conf_info  = classifyRst[index];
                max_confidence = classifyRst[index].confidence;
            }
            face_blur2 = classifyRst[index];
            find_count += 1;
        } else {
            continue;
        }
    }

    if (3 != find_count) {
        return false;
    }

    float blur = 0.f;
    if (max_conf_info.label == kLabelFaceBlur1 || max_conf_info.label == kLabelFaceBlur1Camel) {
        blur = face_blur1.confidence * 0.5f;
    } else if (max_conf_info.label == kLabelFaceBlur2 || max_conf_info.label == kLabelFaceBlur2Camel) {
        blur = face_blur2.confidence;
    } else {
        blur = (1 / std::exp(face_blur1.confidence)) * 0.2f;
    }

    blur = blur * 100;

    faceAngle.confidence = blur;

    return true;
}

constexpr std::string_view kLabelPedQuality0 = "quality0";
constexpr std::string_view kLabelPedQuality1 = "quality1";
constexpr std::string_view kLabelPedQuality2 = "quality2";
constexpr std::string_view kLabelPedQuality3 = "quality3";
constexpr std::string_view kLabelPedQuality4 = "quality4";
constexpr std::string_view kLabelPedQuality5 = "quality5";

bool GetPedQuality(const std::vector<AiConfidence>& classifyRst, AiConfidence& pedQuality) {
    AiConfidence quality0;
    AiConfidence quality4;
    size_t find_count = 0;

    size_t index = 0;
    for (index = 0; index < classifyRst.size(); index++) {
        if ((classifyRst[index].label == kLabelPedQuality0)) {
            quality0 = classifyRst[index];
            find_count += 1;
        } else if ((classifyRst[index].label == kLabelPedQuality4)) {
            quality4 = classifyRst[index];
            find_count += 1;
        } else {
            continue;
        }
    }

    if (2 != find_count) {
        return false;
    }

    pedQuality.confidence = quality0.confidence * 60 + quality4.confidence * 40;
    return true;
}

std::string GetTrackStatus(AITrackingStatus status) {
    switch (status) {
        case AITrackingStatus::TRACKING:
            return "TRACKING";
        case AITrackingStatus::LOSS:
            return "LOSS";
        case AITrackingStatus::NEW:
            return "NEW";
        default:
            return "Unknown";
    }
    return "Unknown";
}

std::string GetMotionStatus(AIMotionState status) {
    switch (status) {
        case AIMotionState::MOVING:
            return "MOVING";
        case AIMotionState::STILL:
            return "STILL";
        case AIMotionState::UNCERTAIN:
            return "UNCERTAIN";
        default:
            return "Unknown";
    }
    return "Unknown";
}

std::string GetShapeChangeStatus(AIShapeChangeState status) {
    switch (status) {
        case AIShapeChangeState::CHANGE:
            return "Shape Change";
        case AIShapeChangeState::STILL:
            return "Shape Still";
        case AIShapeChangeState::UNCERTAIN:
            return "UNCERTAIN";
        default:
            return "Unknown";
    }
    return "Unknown";
}

bool AreaInAreas(const std::vector<TargetAreaUnit>& areas, const TargetAreaUnit& area) {
    auto it = std::find_if(areas.begin(), areas.end(),
                           [&](const TargetAreaUnit& inst) { return inst.area_id == area.area_id; });

    if (it != areas.end()) {
        return true;
    }

    return false;
}

// Helper utilities — moved to AiCommonHelper.cc

}  // namespace cosmo
