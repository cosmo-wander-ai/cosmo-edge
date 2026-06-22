// DetectMsgTypes — Detection types — MsgDetect*, MsgGetFeatures*, MsgAlgorithmPreload*.

#include "DetectMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgDetectRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["eventCodes"]     = v.eventCodes;
    j["mvDebug"]        = v.mvDebug;
    j["imageUrl"]       = v.imageUrl;
    j["imageData"]      = v.imageData;
    j["extParam"]       = v.extParam;
    j["forceOutputAll"] = v.forceOutputAll;
}

void from_json(const nlohmann::json& j, MsgDetectRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("eventCodes").get_to(v.eventCodes);
    JSON_OPT(j, v, mvDebug);
    JSON_OPT(j, v, imageUrl);
    JSON_OPT(j, v, imageData);
    JSON_OPT(j, v, extParam);
    JSON_OPT(j, v, forceOutputAll);
}

void to_json(nlohmann::json& j, const MsgDetectSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["data"] = v.data;
}

void from_json(const nlohmann::json& j, MsgDetectSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, data);
}

void to_json(nlohmann::json& j, const MsgOverviewStructrueRecordRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["switch"] = v.functionSwitch;
}

void from_json(const nlohmann::json& j, MsgOverviewStructrueRecordRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT_KEY(j, v, "switch", functionSwitch);
}

void to_json(nlohmann::json& j, const MsgOverviewStructrueRecordSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["path"] = v.path;
}

void from_json(const nlohmann::json& j, MsgOverviewStructrueRecordSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, path);
}

void to_json(nlohmann::json& j, const MsgAlgorithmPreloadRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmList"] = v.algorithmList;
}

void from_json(const nlohmann::json& j, MsgAlgorithmPreloadRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, algorithmList);
}

void to_json(nlohmann::json& j, const MsgGetFeaturesImage& v) {
    j["imageBase64"] = v.imageBase64;
    j["imageId"]     = v.imageId;
}

void from_json(const nlohmann::json& j, MsgGetFeaturesImage& v) {
    j.at("imageBase64").get_to(v.imageBase64);
    j.at("imageId").get_to(v.imageId);
}

void to_json(nlohmann::json& j, const MsgGetFeaturesFeature& v) {
    j["feature"] = v.feature;
    j["imageId"] = v.imageId;
    j["code"]    = v.code;
    j["rects"]   = v.rects;
}

void from_json(const nlohmann::json& j, MsgGetFeaturesFeature& v) {
    j.at("feature").get_to(v.feature);
    j.at("imageId").get_to(v.imageId);
    j.at("code").get_to(v.code);
    j.at("rects").get_to(v.rects);
}

void to_json(nlohmann::json& j, const MsgGetFeaturesRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["imageList"] = v.imageList;
    j["type"]      = v.type;
    j["quality"]   = v.quality;
}

void from_json(const nlohmann::json& j, MsgGetFeaturesRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("imageList").get_to(v.imageList);
    JSON_OPT(j, v, type);
    JSON_OPT(j, v, quality);
}

void to_json(nlohmann::json& j, const MsgGetFeaturesSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["features"] = v.features;
}

void from_json(const nlohmann::json& j, MsgGetFeaturesSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, features);
}

void from_json(const nlohmann::json& j, MsgPTaskDetectExtParamRule& v) {
    JSON_OPT(j, v, Sensitivity);
    JSON_OPT(j, v, DetectRegion);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectExtParamRule& v) {
    j["Sensitivity"]  = v.Sensitivity;
    j["DetectRegion"] = v.DetectRegion;
}

void from_json(const nlohmann::json& j, MsgPTaskDetectExtParam& v) {
    JSON_OPT(j, v, eventCode);
    JSON_OPT(j, v, rules);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectExtParam& v) {
    j["eventCode"] = v.eventCode;
    j["rules"]     = v.rules;
}

void from_json(const nlohmann::json& j, MsgDetectEventRect& v) {
    JSON_OPT(j, v, left);
    JSON_OPT(j, v, top);
    JSON_OPT(j, v, width);
    JSON_OPT(j, v, height);
}

void to_json(nlohmann::json& j, const MsgDetectEventRect& v) {
    j["left"]   = v.left;
    j["top"]    = v.top;
    j["width"]  = v.width;
    j["height"] = v.height;
}

void from_json(const nlohmann::json& j, MsgDetectEventAttr& v) {
    JSON_OPT(j, v, eventCode);
}

void to_json(nlohmann::json& j, const MsgDetectEventAttr& v) {
    j["eventCode"] = v.eventCode;
}

void from_json(const nlohmann::json& j, MsgDetectEventUnit& v) {
    JSON_OPT(j, v, rect);
    JSON_OPT(j, v, event);
}

void to_json(nlohmann::json& j, const MsgDetectEventUnit& v) {
    j["rect"]  = v.rect;
    j["event"] = v.event;
}

void from_json(const nlohmann::json& j, MsgDetectSend::Data& v) {
    JSON_OPT(j, v, result);
}

void to_json(nlohmann::json& j, const MsgDetectSend::Data& v) {
    j["result"] = v.result;
}

}  // namespace cosmo
