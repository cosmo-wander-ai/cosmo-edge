// Detection types — MsgDetect*, MsgGetFeatures*, MsgAlgorithmPreload*.
// Modular DTO header.

#pragma once

#include "util/dto/TaskCreateTypes.h"

namespace cosmo {

struct MsgPTaskDetectExtParamRule {
    float Sensitivity;
    std::vector<std::vector<float>> DetectRegion;
    friend void to_json(nlohmann::json& j, const MsgPTaskDetectExtParamRule& v);
    friend void from_json(const nlohmann::json& j, MsgPTaskDetectExtParamRule& v);
};

struct MsgPTaskDetectExtParam {
    std::string eventCode;
    std::vector<MsgPTaskDetectExtParamRule> rules;
    friend void to_json(nlohmann::json& j, const MsgPTaskDetectExtParam& v);
    friend void from_json(const nlohmann::json& j, MsgPTaskDetectExtParam& v);
};

struct MsgDetectRecv : public MsgRecvHead {
    std::string mvDebug;
    std::string imageData;  // Photo data in base64
    std::string imageUrl;   // Photo URL
    bool forceOutputAll{false};
    std::vector<std::string> eventCodes;
    std::vector<MsgPTaskDetectExtParam> extParam;
};

void to_json(nlohmann::json& j, const MsgDetectRecv& v);
void from_json(const nlohmann::json& j, MsgDetectRecv& v);

struct MsgDetectEventRect {
    int left{0};
    int top{0};
    int width{0};
    int height{0};
    friend void to_json(nlohmann::json& j, const MsgDetectEventRect& v);
    friend void from_json(const nlohmann::json& j, MsgDetectEventRect& v);
};

struct MsgDetectEventAttr {
    std::string eventCode;
    std::vector<MsgDynamicKeyValue> eventAttr;
    friend void to_json(nlohmann::json& j, const MsgDetectEventAttr& v);
    friend void from_json(const nlohmann::json& j, MsgDetectEventAttr& v);
};

struct MsgDetectEventUnit {
    MsgDetectEventRect rect;
    MsgDetectEventAttr event;
    friend void to_json(nlohmann::json& j, const MsgDetectEventUnit& v);
    friend void from_json(const nlohmann::json& j, MsgDetectEventUnit& v);
};

struct MsgDetectSend : public MsgSendHead {
    struct Data {
        std::vector<MsgDetectEventUnit> result;
        friend void to_json(nlohmann::json& j, const Data& v);
        friend void from_json(const nlohmann::json& j, Data& v);
    } data;
};

void to_json(nlohmann::json& j, const MsgDetectSend& v);
void from_json(const nlohmann::json& j, MsgDetectSend& v);

struct MsgOverviewStructrueRecordRecv : public MsgRecvHead {
    bool functionSwitch{false};
};

void to_json(nlohmann::json& j, const MsgOverviewStructrueRecordRecv& v);
void from_json(const nlohmann::json& j, MsgOverviewStructrueRecordRecv& v);

struct MsgOverviewStructrueRecordSend : public MsgSendHead {
    std::string path;
};

void to_json(nlohmann::json& j, const MsgOverviewStructrueRecordSend& v);
void from_json(const nlohmann::json& j, MsgOverviewStructrueRecordSend& v);

struct MsgAlgorithmPreloadRecv : public MsgRecvHead {
    std::vector<std::string> algorithmList;
};

void to_json(nlohmann::json& j, const MsgAlgorithmPreloadRecv& v);
void from_json(const nlohmann::json& j, MsgAlgorithmPreloadRecv& v);

struct MsgAlgorithmPreloadSend : public MsgSendHead {};

struct MsgGetFeaturesImage {
    std::string imageBase64;
    std::string imageId;
};

void to_json(nlohmann::json& j, const MsgGetFeaturesImage& v);
void from_json(const nlohmann::json& j, MsgGetFeaturesImage& v);

// Feature extraction result codes
namespace feature_code {
    constexpr const char* kSuccess     = "0";    // Success
    constexpr const char* kNoFace      = "-1";   // No face detected
    constexpr const char* kFaceSizeLow = "-2";   // Face region pixel too low
    constexpr const char* kAngle       = "-3";   // Face is not frontal
    constexpr const char* kQuality     = "-4";   // Face quality too low
    constexpr const char* kPartFace    = "-5";   // Incomplete face
    constexpr const char* kMask        = "-6";   // Wearing mask
    constexpr const char* kMultFaces   = "-7";   // Multiple faces detected
    constexpr const char* kAiFailed    = "-8";   // AI engine error
    constexpr const char* kBase64      = "-9";   // Base64 decode error
    constexpr const char* kMem         = "-10";  // Insufficient decode memory
    constexpr const char* kNoAiInst    = "-11";  // AI engine not created
}  // namespace feature_code

struct MsgGetFeaturesFeature {
    std::string feature;
    std::string imageId;
    std::string code;  // Feature extraction error code, 0 = success
    std::vector<MsgRectReal> rects;
};

void to_json(nlohmann::json& j, const MsgGetFeaturesFeature& v);
void from_json(const nlohmann::json& j, MsgGetFeaturesFeature& v);

struct MsgGetFeaturesRecv : public MsgRecvHead {
    std::string type{"1"};  // Type: 1 = face (default), 2 = body
    float quality{-1.0};
    std::vector<MsgGetFeaturesImage> imageList;
};

void to_json(nlohmann::json& j, const MsgGetFeaturesRecv& v);
void from_json(const nlohmann::json& j, MsgGetFeaturesRecv& v);
struct MsgGetFeaturesSend : public MsgSendHead {
    std::vector<MsgGetFeaturesFeature> features;
};

void to_json(nlohmann::json& j, const MsgGetFeaturesSend& v);
void from_json(const nlohmann::json& j, MsgGetFeaturesSend& v);

// Algorithm orchestration

}  // namespace cosmo
