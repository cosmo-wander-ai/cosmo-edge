// Filter/alarm types — MsgAiDetFrame, MsgFilterArea, MsgRecAlarm, etc.
// Shared DTO types used across service, flow, infer, and api layers.

#pragma once

#include "util/dto/TaskAreaTypes.h"

namespace cosmo {

struct MsgAiDetFrame {
    int64_t streamIndex{0};
    int64_t index{0};
    int64_t timestamp{0};
    bool bMsgReq{false};  // Message queue retrieval — avoid duplicate fetching
    std::vector<MsgTarget> targets;
    friend void to_json(nlohmann::json& j, const MsgAiDetFrame& v);
    friend void from_json(const nlohmann::json& j, MsgAiDetFrame& v);
};

struct MsgFilterArea {
    std::string areaId;
    std::string areaName;
    bool bLogicResult{false};  // Logic result from judgment
    float average{-1.0};       // Filter result
    std::string motionStatus;  // Filter status
    MsgRect box;
    MsgRectReal aiBox;
    friend void to_json(nlohmann::json& j, const MsgFilterArea& v);
    friend void from_json(const nlohmann::json& j, MsgFilterArea& v);
};

struct MsgAiFilterFrame {
    int64_t streamIndex{0};
    int64_t index{0};
    int64_t timestamp{0};
    bool bMsgReq{false};  // Message queue retrieval — avoid duplicate fetching
    bool bInputArea{false};
    std::string actionFlowId;
    std::vector<MsgFilterArea> areas;
    std::vector<MsgTarget> targets;
};

// Conditional: CCM(bInputArea, areas), CEC(!bInputArea, targets), CE(areas)
void to_json(nlohmann::json& j, const MsgAiFilterFrame& f);
void from_json(const nlohmann::json& j, MsgAiFilterFrame& f);

struct MsgRecAlarm {
    int64_t streamIndex{0};  // Stream ID
    int64_t index{0};        // Frame sequence
    int64_t timestamp{0};
    bool bMsgReq{false};                    // Message queue retrieval — avoid duplicate fetching
    bool filterAlarmInterval{false};        // Alarm interval
    bool filterTargetAlarmCount{false};     // Target alarm count
    bool filterTargetAlarmInterval{false};  // Target alarm interval
    bool filterPosition{false};             // Static target suppression
    bool filterLlmReview{false};            // LLM review filter
    bool alarm{false};                      // Alarm push
    int trackId{-1};
    std::string areaId;
    int64_t targetCount{0};
    int64_t enterTotalCount{0};
    int64_t leaveTotalCount{0};
    OnEventsPropertyType type{OnEventsPropertyType::None};
    friend void to_json(nlohmann::json& j, const MsgRecAlarm& v);
    friend void from_json(const nlohmann::json& j, MsgRecAlarm& v);
};

struct MsgRecSensitityTarget {
    bool queIsFull{false};        // Sensitivity queue is full — can start calculating
    int trackId{-1};              // Track ID, -1 means no tracking
    std::vector<bool> rsts;       // Historical results
    size_t dem{0};                // Sensitivity denominator
    float sensitity{-1.0};        // Actual sensitivity
    float expertSensitity{-1.0};  // Configured sensitivity threshold
    friend void to_json(nlohmann::json& j, const MsgRecSensitityTarget& v);
    friend void from_json(const nlohmann::json& j, MsgRecSensitityTarget& v);
};
struct MsgRecArea {
    std::string areaId;
    std::vector<MsgRecSensitityTarget> targets;
    friend void to_json(nlohmann::json& j, const MsgRecArea& v);
    friend void from_json(const nlohmann::json& j, MsgRecArea& v);
};

struct MsgRecSensitity {
    int64_t streamIndex{0};  // Stream ID
    int64_t index{0};        // Frame sequence
    int64_t timestamp{0};
    bool bMsgReq{false};  // Message queue retrieval — avoid duplicate fetching
    std::vector<MsgRecArea> areas;
    friend void to_json(nlohmann::json& j, const MsgRecSensitity& v);
    friend void from_json(const nlohmann::json& j, MsgRecSensitity& v);
};

struct MsgRecPosSaveSensitityTarget {
    int trackId{-1};               // Track ID, -1 means no tracking
    bool behaviorDetected{false};  // Whether behavior was detected
    uint64_t duration{0};          // Target appearance duration in milliseconds
    std::vector<bool> rsts;        // Historical results
    MsgRect box;                   // Target position
    MsgRectReal aiBox;             // Target position
    friend void to_json(nlohmann::json& j, const MsgRecPosSaveSensitityTarget& v);
    friend void from_json(const nlohmann::json& j, MsgRecPosSaveSensitityTarget& v);
};

struct MsgRecPosSaveSensitity {
    int64_t streamIndex{0};  // Stream ID
    int64_t index{0};        // Frame sequence
    int64_t timestamp{0};
    bool bMsgReq{false};  // Message queue retrieval — avoid duplicate fetching
    std::vector<MsgRecPosSaveSensitityTarget> targets;
    friend void to_json(nlohmann::json& j, const MsgRecPosSaveSensitity& v);
    friend void from_json(const nlohmann::json& j, MsgRecPosSaveSensitity& v);
};

struct MsgLogicTestRecv : public MsgRecvHead {
    std::string taskId;
    MsgAiDetFrame frame;
};
void to_json(nlohmann::json& j, const MsgLogicTestRecv& r);
void from_json(const nlohmann::json& j, MsgLogicTestRecv& r);

struct MsgLogicTestSend : public MsgSendHead {
    std::string taskId;
    MsgAiDetFrame frame;
};
void to_json(nlohmann::json& j, const MsgLogicTestSend& s);
void from_json(const nlohmann::json& j, MsgLogicTestSend& s);

}  // namespace cosmo
