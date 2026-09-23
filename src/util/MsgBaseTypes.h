// Base message types — MsgRecvHead, MsgSendHead, geometry types, enums.
// Modular DTO header.
#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "util/LimitedType.h"
#include "util/MsgDynamicElement.h"

namespace cosmo {

// Server response codes (resCode)
constexpr int kServerRspSuccess = 1;
constexpr int kServerRspFailed  = 0;

// Client response codes from management server (resCode)
constexpr int kClientRspSuccess = 1;
constexpr int kClientRspFailed  = 0;

enum class MessageFromType { MessageFromHttp = 0, MessageFromMqtt = 1 };

enum class Operation {
    Delete = 0,
    Add    = 1,
    Update = 2,
};

// Common fields for received messages.
// Empty base class — no JSON fields to serialize.
struct MsgRecvHead {
    MessageFromType messageFrom{MessageFromType::MessageFromHttp};
};

// No-op serialization for empty base class
void to_json(nlohmann::json& j, const MsgRecvHead& /*h*/);
void from_json(const nlohmann::json& /*j*/, MsgRecvHead& /*h*/);

struct MsgErrorDetails {
    std::optional<std::uint64_t> actualBytes;
    std::optional<std::uint64_t> limitBytes;
    std::optional<std::uint64_t> requiredBytes;
    std::optional<std::uint64_t> availableBytes;
    std::optional<std::uint64_t> reserveBytes;
    std::optional<std::uint64_t> actualCount;
    std::optional<std::uint64_t> limitCount;
    std::string resource;
    std::string purpose;

    [[nodiscard]] bool Empty() const noexcept;
    friend void to_json(nlohmann::json& j, const MsgErrorDetails& v);
    friend void from_json(const nlohmann::json& j, MsgErrorDetails& v);
};

struct MsgResBase {
    std::string msgCode;
    std::string messageKey;
    std::string msgText;
    MsgErrorDetails details;
    bool retryable{false};
    std::string recommendedAction;
    std::optional<std::uint32_t> retryAfterSeconds;
    friend void to_json(nlohmann::json& j, const MsgResBase& v);
    friend void from_json(const nlohmann::json& j, MsgResBase& v);
};

enum class MsgSendType {
    CWAI = 0,     // CWAI management device
    ChinaMobile,  // China Mobile
    Max,
};

enum class OnEventsPropertyType {
    None = 0,
    Face,
    Body,
    Vehicle,
    Behavior,
    MachineMaterial,
    People,
    Car,
    PersonCount,
    CountNumber,
    BodyFeature,
    WorkClothesRecognition,
    Attributes,
    Max,
};

// Common fields for outgoing messages.
// Conditional serialization: CWAI uses resCode/resMsg, ChinaMobile uses resultCode/resultMsg.
struct MsgSendHead {
    MsgSendType msgSendType{MsgSendType::CWAI};
    int resCode{0};  // 1: success, 0: failure
    std::vector<MsgResBase> resMsg;

    std::string resultCode;  // 1: success, 0: failure
    std::string resultMsg;
};

void to_json(nlohmann::json& j, const MsgSendHead& h);
void from_json(const nlohmann::json& j, MsgSendHead& h);

// Geometry types with JSON aliases
struct MsgPoint {
    double x{0.0};
    double y{0.0};
};
void to_json(nlohmann::json& j, const MsgPoint& p);
void from_json(const nlohmann::json& j, MsgPoint& p);

struct MsgRect {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};
void to_json(nlohmann::json& j, const MsgRect& r);
void from_json(const nlohmann::json& j, MsgRect& r);

struct MsgRectReal {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};
// MsgRectReal: all fields mandatory
void to_json(nlohmann::json& j, const MsgRectReal& r);
void from_json(const nlohmann::json& j, MsgRectReal& r);

enum class MsgInputAreaType {
    All = 0,  // All areas
    Main,     // Main area
    Asso,     // Associated area
    Max,
};
constexpr bool IsValidInputAreaType(int value) {
    return value >= static_cast<int>(MsgInputAreaType::All) &&
           value < static_cast<int>(MsgInputAreaType::Max);
}

// Target association type
enum class MsgTargetAssoType {
    None = 0,          // None
    PersonFace,        // Person-face association
    SubInMain,         // Contained
    SubIntersectMain,  // Intersecting
    SubWestMain,       // Sub-target west of main
    SubEastMain,       // Sub-target east of main
    SubNorthMain,      // Sub-target north of main
    SubSouthMain,      // Sub-target south of main
    Max,
};
constexpr bool IsValidTargetAssoType(int value) {
    return value > static_cast<int>(MsgTargetAssoType::None) &&
           value < static_cast<int>(MsgTargetAssoType::Max);
}

enum class FeatureInputType {
    Face = 0,       // Face detection — feature upload
    Body,           // Work clothes detection — feature comparison then to sensitivity module
    Things,         // Machine/object detection — feature comparison then to sensitivity module
    BodyNoCompare,  // Body feature — no comparison, direct alarm
    Max,
};
constexpr bool IsValidFeatureInputType(int value) {
    return value >= static_cast<int>(FeatureInputType::Face) &&
           value < static_cast<int>(FeatureInputType::Max);
}

}  // namespace cosmo
