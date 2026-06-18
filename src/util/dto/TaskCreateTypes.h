// Task creation types — MsgTaskCreateRecv, MsgPTaskCreateRecv, MsgPTaskDetectPic*, etc.

#pragma once

#include "util/dto/TaskAreaTypes.h"

namespace cosmo {

// Common message struct, task creation request for server, CMsgQueryTaskListRsp response for client
// Create task request
struct MsgTaskCreateRecv : public MsgRecvHead {
    std::string mvDebug;
    std::string taskId;            // Task ID, globally unique
    std::string taskDesc;          // Task description
    std::string videoChannelId;    // Channel ID
    std::string videoChannelName;  // Channel name
    std::string streamUrl;         // Channel URL

    std::string algorithmId;          // Algorithm ID
    std::string algorithmCategory;    // Algorithm category, consistent with authorization platform
    std::string algorithmCode;        // Algorithm code
    std::string algorithmVersion;     // Algorithm version
    std::string algorithmName;        // Algorithm name
    std::string algorithmCheckSum;    // Algorithm checksum
    std::string algorithmUpdateTime;  // Algorithm update timestamp (13 digits)
    MsgTaskConfig taskConfig;
};

// I(MsgRecvHead), M(taskId, videoChannelId, algorithmCode, algorithmUpdateTime), O(rest...)
void to_json(nlohmann::json& j, const MsgTaskCreateRecv& r);
void from_json(const nlohmann::json& j, MsgTaskCreateRecv& r);

// Create task response
struct MsgTaskCreateSend : public MsgSendHead {};

//
// Create picture algorithm task request
struct MsgPTaskCreateRecv : public MsgRecvHead {
    std::string mvDebug;
    std::string taskId;    // Task ID, globally unique
    std::string taskDesc;  // Task description

    std::string algorithmId;          // Algorithm ID
    std::string algorithmCategory;    // Algorithm category, consistent with authorization platform
    std::string algorithmCode;        // Algorithm code
    std::string algorithmVersion;     // Algorithm version
    std::string algorithmName;        // Algorithm name
    std::string algorithmCheckSum;    // Algorithm checksum
    std::string algorithmUpdateTime;  // Algorithm update timestamp (13 digits)
    MsgTaskConfig taskConfig;
};

void to_json(nlohmann::json& j, const MsgPTaskCreateRecv& r);
void from_json(const nlohmann::json& j, MsgPTaskCreateRecv& r);

// Create task response
struct MsgPTaskCreateSend : public MsgSendHead {};

struct MsgPTaskDetectPicRecv : public MsgRecvHead {
    std::string mvDebug;
    std::string taskId;         // Task ID, globally unique
    std::string algorithmCode;  // Algorithm code
    std::string imageBase64;    // Picture base64
    std::string imageUrl;       // Picture URL
    bool needRetImg{true};      // Need overlay picture

    MsgTaskConfig taskConfig;
};

void to_json(nlohmann::json& j, const MsgPTaskDetectPicRecv& r);
void from_json(const nlohmann::json& j, MsgPTaskDetectPicRecv& r);

struct MsgPTaskTarget {
    bool bHaveLogicResult{false};
    bool bLogicResult{false};
    MsgRectReal box;
    std::vector<MsgAiConfidence> confidence;
    std::vector<int> groupEls;
    bool bHaveMatchInfo{false};
    MsgMatchInfo matchInfo;
    std::vector<MsgPoint> maskPolygon;  // SAM2 segmentation contour points (normalized 0-1)
    std::vector<MsgPoint> landmark;     // Landmark coordinate list (pixel coordinates)
    std::string featurePreview;         // Feature value first 10 bits preview (comma-separated floats)
};

// Conditional: bLogicResult, confidence, groupEls, matchInfo, maskPolygon, landmark, featurePreview
void to_json(nlohmann::json& j, const MsgPTaskTarget& t);
void from_json(const nlohmann::json& j, MsgPTaskTarget& t);

struct MsgPTaskArea {
    std::string areaId;
    std::string areaName;
    bool bDetected{false};
    std::vector<MsgPTaskTarget> targetList;
    friend void to_json(nlohmann::json& j, const MsgPTaskArea& v);
    friend void from_json(const nlohmann::json& j, MsgPTaskArea& v);
};

// Create task response
struct MsgPTaskDetectPicSend : public MsgSendHead {
    struct ResData {
        std::string algorithmCode;
        std::string timestamp;
        std::string fullPicture;
        std::vector<MsgPTaskArea> areaList;
        std::vector<MsgPTaskTarget> targetList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgPTaskDetectPicSend& s);
void from_json(const nlohmann::json& j, MsgPTaskDetectPicSend& s);

}  // namespace cosmo
