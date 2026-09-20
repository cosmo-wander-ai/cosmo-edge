// AiCommon.h — Common AI detection/tracking/classification data types.
#pragma once

#include "media/VideoFrame.h"
#include "util/AiTypes.h"

namespace cosmo {

// A missing/invalid observation must never be interpreted as a negative match.
enum class FaceObservationStatus {
    kNotObserved,
    kNoFace,
    kFiltered,
    kLowQuality,
    kUnavailable,
    kMatched,
    kUnmatched,
};

struct FaceObservation {
    FaceObservationStatus status{FaceObservationStatus::kNotObserved};
    float quality{-1.0f};
};

enum class TargetAssociationStatus {
    kNotObserved,
    kMatched,
    kMissing,
    kAmbiguous,
    kTooSmall,
    kFiltered,
    kUnavailable,
};

struct AiDetectBestEl {
    bool bActive{false};      // Whether a best snapshot exists
    float bestQuality{-1.0};  // Best quality score
    util::Box box;            // Best target position
    VideoFramePtr bestFrame;  // Best snapshot frame
};

struct AiDetectRelatedEl {
    bool bActive{false};  // Whether an unambiguous related target was detected
    AiConfidence confidence;
    util::Box box;
    std::vector<AiConfidence> classifyRst;
    AiLandmarkData landmark;  // Landmark from box or relatedEl
    AiFeature feature;        // Feature vector for face matching
};

// Detection / tracking / classification result
struct AiDetectRstEl {
    util::Box box;
    util::Box scaleBox;    // Box after padding/scaling
    std::string targetId;  // Target ID — globally unique, used for target association
    util::Point point;
    size_t frameIndex{0};
    size_t streamIndex{0};
    float hwRatio{0.0};           // Height-to-width ratio
    float hwRatioVariation{0.0};  // Height-to-width ratio variation
    AiConfidence confidence;

    int classId{-1};
    int trackId{-1};
    std::string trackIdInfo;  // Globally unique tracking UUID
    AITrackingStatus trackStatus{AITrackingStatus::UNKNOW};
    AIMotionState motionStatus{AIMotionState::UNCERTAIN};
    AIShapeChangeState shapeChangeStatus{AIShapeChangeState::UNCERTAIN};

    std::string algCode{};
    bool bFilter{false};
    bool bForceClassify{false};
    bool bLogicResult{false};  // Sensitivity judgement basis
    std::string filterDesc{};
    AIFilterType filterType{AIFilterType::None};
    std::vector<AiConfidence> classifyRst;
    std::vector<AiAttribute> attrRst;  // Attribute results
    std::vector<AiOcrValue> ocrRst;    // OCR results
    TargetAreaSign areaSign;
    TargetPosition targetPos{TargetPosition::kBottom};
    std::vector<int> groupTargets;              // Group target trackIds
    AiDetectRelatedEl relatedEl;                // Related target info (e.g. body-face)
    std::vector<AiDetectRelatedEl> relatedEls;  // Related targets — multi-target association
    std::vector<AiDetectRelatedEl> subs;        // Associated targets
    AiDetectBestEl bestEl;
    AiLandmarkData landmark;  // Landmark from box or relatedEl
    AiFeature feature;        // Feature vector for face matching

    AiMask mask;  // Segmentation result

    AiDetectMatchHighScoreInfo matchInfo;  // Highest-score match info during comparison
    FaceObservation face_observation;
    TargetAssociationStatus association_status{TargetAssociationStatus::kNotObserved};
};

using AiDetectRstElPtr = std::shared_ptr<AiDetectRstEl>;

// Group target
struct AiGroupEl {
    int groupId{-1};
    std::string groupIdInfo;
    bool bLogicResult{false};
    double distance{-1.0};
    AiDetectRstEl genTarget;
    std::vector<AiDetectRstEl> srcTargets;
};
using AiGroupElPtr = std::shared_ptr<AiGroupEl>;

bool GetFaceAngle(const std::vector<AiConfidence>& classifyRst, AiConfidence& faceAngle);
bool GetFaceQuality(const std::vector<AiConfidence>& classifyRst, AiConfidence& faceAngle);
bool GetFaceAngleType(const AiConfidence& faceAngle, FaceAngle& angle);

bool GetPedQuality(const std::vector<AiConfidence>& classifyRst, AiConfidence& pedQuality);

std::string GetTrackStatus(AITrackingStatus status);
std::string GetMotionStatus(AIMotionState status);
std::string GetShapeChangeStatus(AIShapeChangeState status);

bool AreaInAreas(const std::vector<TargetAreaUnit>& areas, const TargetAreaUnit& area);
bool areaIdInAreaUnits(const std::vector<TargetAreaUnit>& areas, const std::string& area);

// Validate license plate after OCR recognition
bool ValidatePlate(const std::string& input, std::string& output, size_t& wordSize);

std::string GBVehicleType(const std::vector<AiAttribute>& attrRst);
GBVehicleColorType GBVehicleColor(const std::vector<AiAttribute>& attrRst);
GBVehicleDirectionType GBVehicleDirection(const std::vector<AiAttribute>& attrRst);

}  // namespace cosmo
