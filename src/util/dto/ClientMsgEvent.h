// Message types acting as client

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/AiTypes.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

struct CMsgOnEventsPropertyFace {
    float quality{-1.0};     // Face quality: -1 not detected
    int age{-1};             // Age: -1 not detected
    int gender{-1};          // Face image detected gender (-1: undetected, 0: female, 1: male)
    int wearMask{-1};        // Wearing mask (-1: undetected, 1: wearing, 0: not wearing)
    int wearGlasses{-1};     // Wearing glasses (-1: undetected, 1: wearing, 0: not wearing)
    std::string featureUrl;  // Face feature file
    std::string image;       // URL of face image
    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyFace& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyFace& v);
};

struct CMsgOnEventsPropertyRecognition {
    float matchDegree{-0.1f};  // Similarity
    std::string matchLibName;  // Name of the face library
    std::string matchId;
    std::string LibImage;    // URL of the face library image
    std::string matchName;   // Name
    std::string personCode;  // Person code
    std::string personId;    // Person ID
    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyRecognition& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyRecognition& v);
};

enum class OnEventsBodyPropertyType {
    None = 0,
    Attr,
    Feature

    ,
    Max  //
};

struct CMsgOnEventsPropertyBody {
    OnEventsBodyPropertyType type{OnEventsBodyPropertyType::None};
    int topLength{-1};
    int topColor{-1};
    int bottomLength{-1};
    int bottomColor{-1};
    int inhand{-1};
    float quality{-1.0};
    std::string featureUrl;  // Feature URL
    std::string feature;     // Feature
    std::string image;       // Body image
    std::string inAreaTime;
    std::string inAreaFullImageUrl;
    std::string outAreaTime;
    std::string outAreaFullImageUrl;
};

// Conditional: CCM(Attr), CEC(Feature), CE(fallback)
void to_json(nlohmann::json& j, const CMsgOnEventsPropertyBody& b);
void from_json(const nlohmann::json& j, CMsgOnEventsPropertyBody& b);

struct CMsgAiAttr {
    std::string category;    // Category
    std::string label;       // Label
    std::string atomicCode;  // Code
    float confidence{0.0};   // Label confidence

    friend void to_json(nlohmann::json& j, const CMsgAiAttr& v);
    friend void from_json(const nlohmann::json& j, CMsgAiAttr& v);
};

struct CMsgOnEventsPropertyVehicle {
    std::string plateColor;    // License plate color
    std::string vehicleColor;  // Vehicle body color
    std::string vehicleClass;  // Vehicle type
    std::string orientation;   // Vehicle orientation (1: front, 2: back, 3: side, 9: bad quality)
    std::string plate;         // License plate number
    std::string plateSrc;      // License plate number (original OCR string)

    std::vector<CMsgAiAttr> attrs;
    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyVehicle& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyVehicle& v);
};

struct CMsgOnEventsPropertyBehavior {
    int count{-1};  // Entered people count during detection time (e.g. for people entering well/cage)
    int duration{
        -1};  // Continuous violation duration of the same target, in seconds (e.g. sleeping for 300s)
    std::string targetId;  // Target ID. Unique UUID generated for the tracked violation target. Sleeping
                           // algorithms represent the human body, absence algorithms represent the area.
                           // Destroyed when violation is lifted.

    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyBehavior& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyBehavior& v);
};

struct CMsgOnEventsPropertyPeople {
    int enterNumber{
        0};  // Cumulative entered people, accumulated per hour, overwrites previous data if repeatedly sent
    int leaveNumber{
        0};  // Cumulative departed people, accumulated per hour, overwrites previous data if repeatedly sent
    int enterOrgNum{0};  // Entered people during interval, not cumulatively added
    int leaveOrgNum{0};  // Departed people during interval, not cumulatively added
    std::string time;    // Reported time accurate to seconds (e.g. 2020-01-01 10:12:15)

    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyPeople& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyPeople& v);
};

struct CMsgOnEventsPropertyPersonInfo {
    std::string orignalPicture;
    std::string fullPicture;
    std::string targetPicture;
    MsgRectReal box;
    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyPersonInfo& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyPersonInfo& v);
};

struct CMsgOnEventsPropertyTarget {
    std::string inAreaTime;
    std::string inAreaFullImageUrl;
    std::string outAreaTime;
    std::string outAreaFullImageUrl;
    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyTarget& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyTarget& v);
};

// Matching result
struct CMsgOnEventsPropertyMatchRst {
    std::string matchId;       // Matched target ID (highest similarity even if threshold not met)
    float matchDegree{-1.0};   // Similarity
    std::string groupId;       // Matched target library ID
    std::string groupName;     // Matched target library name
    std::string baseImageUrl;  // Comparison base library image

    friend void to_json(nlohmann::json& j, const CMsgOnEventsPropertyMatchRst& v);
    friend void from_json(const nlohmann::json& j, CMsgOnEventsPropertyMatchRst& v);
};

// Filter status
enum class OnEventsFilterStatus {
    None        = 0,  // No status
    Move2Static = 1,  // Moving to static
    Static2Move = 2,  // Static to moving
    Static      = 10,
    Move        = 11,
    Max,
};
/// Validate that a filter status value is within the valid enum range.
constexpr bool IsValidFilterStatus(int value) noexcept {
    return value > static_cast<int>(OnEventsFilterStatus::None) &&
           value < static_cast<int>(OnEventsFilterStatus::Max);
}

struct CMsgOnEventsPropertyMachineMaterial : public CMsgOnEventsPropertyMatchRst {
    std::string runningStatus;
};

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyMachineMaterial& m);
void from_json(const nlohmann::json& j, CMsgOnEventsPropertyMachineMaterial& m);

enum class OnEventsReportType {
    Trigger = 0,  // Triggered event, usually requires recording video
    Realtime,     // Periodically reported event, usually no video recording needed
    Max,
};

struct CMsgOnEventsProperty {
    OnEventsPropertyType type{OnEventsPropertyType::None};
    CMsgOnEventsPropertyFace face;                        // Face detection
    CMsgOnEventsPropertyRecognition recognition;          // Face recognition
    CMsgOnEventsPropertyBody body;                        // Body attributes
    CMsgOnEventsPropertyVehicle vehicle;                  // Vehicle attributes
    CMsgOnEventsPropertyBehavior behavior;                // Behavior-based special detection attributes
    CMsgOnEventsPropertyMachineMaterial machineMaterial;  // Machine material special detection attributes
    CMsgOnEventsPropertyPeople
        people;  // People counting (cumulative count reset every hour) People counting attribute
    CMsgOnEventsPropertyPeople car;  // Car traffic counting (cumulative count reset every hour)
    CMsgOnEventsPropertyMatchRst workClothesRecognition;  // Work clothes comparison library info
    int personCount{-1};                                  // Crowd gathering count
    std::vector<CMsgOnEventsPropertyPersonInfo> persons;  // Personnel
    int countNumber{-1};  // People counting (not an alarm, high-frequency stream data) Count (based on single
                          // frame people counting result)
    bool bHaveTarget{false};
    CMsgOnEventsPropertyTarget target;  // Target info
};

// Conditional: CCM/CEC/CE by type, CC(bHaveTarget, target)
void to_json(nlohmann::json& j, const CMsgOnEventsProperty& p);
void from_json(const nlohmann::json& j, CMsgOnEventsProperty& p);

// Analysis machine reporting event interface
struct CMsgOnEventsReq : public MsgRecvHead {
    std::string messageId;
    std::string devId;               // Device ID
    std::string taskId;              // Task ID
    std::string videoChannelId;      // Channel
    std::string channelName;         // Channel name
    std::string timestamp;           // UTC timestamp ms
    int64_t itimestamp{0};           // Timestamp ms
    std::string algorithmId;         // Algorithm ID
    std::string algorithmCode;       // Algorithm Code
    std::string algorithmName;       // Algorithm name
    std::string areaId;              // Area ID
    std::string areaName;            // Area name
    std::string orignalPicture;      // Original picture URL
    std::string fullPicture;         // Full picture URL
    std::string detectedPicture;     // Detected picture URL
    std::string video;               // Alarm video URL
    std::string videostructured;     // Video structured file URL
    std::string overviewFile;        // Video structured overview file URL
    std::string recordId;            // Alarm record ID (trackId?)
    std::vector<std::string> files;  // Recorded files
    bool isRetryMessage{false};      // Whether it is a retried message
    bool bHaveProperty{false};       // Whether property field exists
    CMsgOnEventsProperty property;   // Property
    std::string category;            // Category
};

// Conditional: CC(bHaveProperty, property)
void to_json(nlohmann::json& j, const CMsgOnEventsReq& r);
void from_json(const nlohmann::json& j, CMsgOnEventsReq& r);

// Analysis machine reporting event response
struct CMsgOnEventsRsp : public MsgSendHead {};

// Analysis machine reporting face event interface
struct CMsgFaceEventReq : public MsgRecvHead {
    std::string messageId;
    std::string devId;           // Device ID
    std::string taskId;          // Task ID
    std::string videoChannelId;  // Channel
    std::string timestamp;       // UTC timestamp ms
    std::string algorithmId;     // Algorithm ID
    std::string algorithmCode;   // Algorithm Code
    std::string algorithmName;   // Algorithm name
    std::string recordId;        // Alarm record ID (trackId?)

    std::string featureUrl;  // Face feature file
    AiFeature feature;       // Feature value used during face comparison;
    std::string image;       // URL of face image
};

// Analysis machine reporting face event interface response
struct CMsgFaceEventRsp : public MsgSendHead {};

}  // namespace cosmo
