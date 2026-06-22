// ClientMsgEvent — Message types acting as client

#include "ClientMsgEvent.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgOnEventsPropertyBody& b) {
    j["image"] = b.image;
    if (OnEventsBodyPropertyType::Attr == b.type) {
        j["topLength"]    = b.topLength;
        j["topColor"]     = b.topColor;
        j["bottomLength"] = b.bottomLength;
        j["bottomColor"]  = b.bottomColor;
        j["inhand"]       = b.inhand;
    } else if (OnEventsBodyPropertyType::Feature == b.type) {
        j["quality"]             = b.quality;
        j["inAreaTime"]          = b.inAreaTime;
        j["inAreaFullImageUrl"]  = b.inAreaFullImageUrl;
        j["outAreaTime"]         = b.outAreaTime;
        j["outAreaFullImageUrl"] = b.outAreaFullImageUrl;
        j["featureUrl"]          = b.featureUrl;
        j["feature"]             = b.feature;
    } else {
        j["topLength"]    = b.topLength;
        j["topColor"]     = b.topColor;
        j["bottomLength"] = b.bottomLength;
        j["bottomColor"]  = b.bottomColor;
        j["inhand"]       = b.inhand;
    }
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyBody& b) {
    JSON_OPT(j, b, image);
    JSON_OPT(j, b, topLength);
    JSON_OPT(j, b, topColor);
    JSON_OPT(j, b, bottomLength);
    JSON_OPT(j, b, bottomColor);
    JSON_OPT(j, b, inhand);
    JSON_OPT(j, b, quality);
    JSON_OPT(j, b, featureUrl);
    JSON_OPT(j, b, feature);
    JSON_OPT(j, b, inAreaTime);
    JSON_OPT(j, b, inAreaFullImageUrl);
    JSON_OPT(j, b, outAreaTime);
    JSON_OPT(j, b, outAreaFullImageUrl);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyMachineMaterial& m) {
    j["runningStatus"] = m.runningStatus;
    // Base class fields
    to_json(j, static_cast<const CMsgOnEventsPropertyMatchRst&>(m));
    // Merge base fields into same object
    nlohmann::json base;
    to_json(base, static_cast<const CMsgOnEventsPropertyMatchRst&>(m));
    j.update(base);
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyMachineMaterial& m) {
    from_json(j, static_cast<CMsgOnEventsPropertyMatchRst&>(m));
    JSON_OPT(j, m, runningStatus);
}

void to_json(nlohmann::json& j, const CMsgOnEventsProperty& p) {
    if (OnEventsPropertyType::Face == p.type) {
        j["face"]        = p.face;
        j["recognition"] = p.recognition;
    } else if (OnEventsPropertyType::Body == p.type) {
        j["body"] = p.body;
    } else if (OnEventsPropertyType::Vehicle == p.type) {
        j["vehicle"] = p.vehicle;
    } else if (OnEventsPropertyType::Behavior == p.type) {
        j["behavior"] = p.behavior;
    } else if (OnEventsPropertyType::MachineMaterial == p.type) {
        j["machineMaterial"] = p.machineMaterial;
    } else if (OnEventsPropertyType::People == p.type) {
        j["people"] = p.people;
    } else if (OnEventsPropertyType::Car == p.type) {
        j["car"] = p.car;
    } else if (OnEventsPropertyType::PersonCount == p.type) {
        j["personCount"] = p.personCount;
        j["persons"]     = p.persons;
    } else if (OnEventsPropertyType::CountNumber == p.type) {
        j["countNumber"] = p.countNumber;
    } else if (OnEventsPropertyType::BodyFeature == p.type) {
        j["body"] = p.body;
    } else if (OnEventsPropertyType::WorkClothesRecognition == p.type) {
        j["workClothesRecognition"] = p.workClothesRecognition;
    } else {
        j["face"] = p.face;
    }
    if (p.bHaveTarget)
        j["target"] = p.target;
}

void from_json(const nlohmann::json& j, CMsgOnEventsProperty& p) {
    JSON_OPT(j, p, face);
    JSON_OPT(j, p, recognition);
    JSON_OPT(j, p, body);
    JSON_OPT(j, p, vehicle);
    JSON_OPT(j, p, behavior);
    JSON_OPT(j, p, machineMaterial);
    JSON_OPT(j, p, people);
    JSON_OPT(j, p, car);
    JSON_OPT(j, p, personCount);
    JSON_OPT(j, p, persons);
    JSON_OPT(j, p, countNumber);
    JSON_OPT(j, p, workClothesRecognition);
    if (auto it = j.find("target"); it != j.end() && !it->is_null()) {
        p.bHaveTarget = true;
        it->get_to(p.target);
    }
}

void to_json(nlohmann::json& j, const CMsgOnEventsReq& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["messageId"]       = r.messageId;
    j["devId"]           = r.devId;
    j["taskId"]          = r.taskId;
    j["videoChannelId"]  = r.videoChannelId;
    j["channelName"]     = r.channelName;
    j["timestamp"]       = r.timestamp;
    j["algorithmId"]     = r.algorithmId;
    j["algorithmCode"]   = r.algorithmCode;
    j["algorithmName"]   = r.algorithmName;
    j["areaId"]          = r.areaId;
    j["areaName"]        = r.areaName;
    j["orignalPicture"]  = r.orignalPicture;
    j["fullPicture"]     = r.fullPicture;
    j["detectedPicture"] = r.detectedPicture;
    j["video"]           = r.video;
    j["videostructured"] = r.videostructured;
    j["overviewFile"]    = r.overviewFile;
    j["recordId"]        = r.recordId;
    j["isRetryMessage"]  = r.isRetryMessage;
    j["category"]        = r.category;
    if (r.bHaveProperty)
        j["property"] = r.property;
}

void from_json(const nlohmann::json& j, CMsgOnEventsReq& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    JSON_OPT(j, r, messageId);
    JSON_OPT(j, r, devId);
    JSON_OPT(j, r, taskId);
    JSON_OPT(j, r, videoChannelId);
    JSON_OPT(j, r, channelName);
    JSON_OPT(j, r, timestamp);
    JSON_OPT(j, r, algorithmId);
    JSON_OPT(j, r, algorithmCode);
    JSON_OPT(j, r, algorithmName);
    JSON_OPT(j, r, areaId);
    JSON_OPT(j, r, areaName);
    JSON_OPT(j, r, orignalPicture);
    JSON_OPT(j, r, fullPicture);
    JSON_OPT(j, r, detectedPicture);
    JSON_OPT(j, r, video);
    JSON_OPT(j, r, videostructured);
    JSON_OPT(j, r, overviewFile);
    JSON_OPT(j, r, recordId);
    JSON_OPT(j, r, isRetryMessage);
    JSON_OPT(j, r, category);
    if (auto it = j.find("property"); it != j.end() && !it->is_null()) {
        r.bHaveProperty = true;
        it->get_to(r.property);
    }
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyFace& v) {
    JSON_OPT(j, v, quality);
    JSON_OPT(j, v, age);
    JSON_OPT(j, v, gender);
    JSON_OPT(j, v, wearMask);
    JSON_OPT(j, v, wearGlasses);
    JSON_OPT(j, v, featureUrl);
    JSON_OPT(j, v, image);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyFace& v) {
    j["quality"]     = v.quality;
    j["age"]         = v.age;
    j["gender"]      = v.gender;
    j["wearMask"]    = v.wearMask;
    j["wearGlasses"] = v.wearGlasses;
    j["featureUrl"]  = v.featureUrl;
    j["image"]       = v.image;
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyRecognition& v) {
    JSON_OPT(j, v, matchDegree);
    JSON_OPT(j, v, matchLibName);
    JSON_OPT(j, v, matchId);
    JSON_OPT(j, v, LibImage);
    JSON_OPT(j, v, matchName);
    JSON_OPT(j, v, personCode);
    JSON_OPT(j, v, personId);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyRecognition& v) {
    j["matchDegree"]  = v.matchDegree;
    j["matchLibName"] = v.matchLibName;
    j["matchId"]      = v.matchId;
    j["LibImage"]     = v.LibImage;
    j["matchName"]    = v.matchName;
    j["personCode"]   = v.personCode;
    j["personId"]     = v.personId;
}

void from_json(const nlohmann::json& j, CMsgAiAttr& v) {
    JSON_OPT(j, v, category);
    JSON_OPT(j, v, label);
    JSON_OPT(j, v, atomicCode);
    JSON_OPT(j, v, confidence);
}

void to_json(nlohmann::json& j, const CMsgAiAttr& v) {
    j["category"]   = v.category;
    j["label"]      = v.label;
    j["atomicCode"] = v.atomicCode;
    j["confidence"] = v.confidence;
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyVehicle& v) {
    JSON_OPT(j, v, plateColor);
    JSON_OPT(j, v, vehicleColor);
    JSON_OPT(j, v, vehicleClass);
    JSON_OPT(j, v, orientation);
    JSON_OPT(j, v, plate);
    JSON_OPT(j, v, plateSrc);
    JSON_OPT(j, v, attrs);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyVehicle& v) {
    j["plateColor"]   = v.plateColor;
    j["vehicleColor"] = v.vehicleColor;
    j["vehicleClass"] = v.vehicleClass;
    j["orientation"]  = v.orientation;
    j["plate"]        = v.plate;
    j["plateSrc"]     = v.plateSrc;
    j["attrs"]        = v.attrs;
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyBehavior& v) {
    JSON_OPT(j, v, count);
    JSON_OPT(j, v, duration);
    JSON_OPT(j, v, targetId);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyBehavior& v) {
    j["count"]    = v.count;
    j["duration"] = v.duration;
    j["targetId"] = v.targetId;
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyPeople& v) {
    JSON_OPT(j, v, enterNumber);
    JSON_OPT(j, v, leaveNumber);
    JSON_OPT(j, v, enterOrgNum);
    JSON_OPT(j, v, leaveOrgNum);
    JSON_OPT(j, v, time);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyPeople& v) {
    j["enterNumber"] = v.enterNumber;
    j["leaveNumber"] = v.leaveNumber;
    j["enterOrgNum"] = v.enterOrgNum;
    j["leaveOrgNum"] = v.leaveOrgNum;
    j["time"]        = v.time;
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyPersonInfo& v) {
    JSON_OPT(j, v, orignalPicture);
    JSON_OPT(j, v, fullPicture);
    JSON_OPT(j, v, targetPicture);
    JSON_OPT(j, v, box);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyPersonInfo& v) {
    j["orignalPicture"] = v.orignalPicture;
    j["fullPicture"]    = v.fullPicture;
    j["targetPicture"]  = v.targetPicture;
    j["box"]            = v.box;
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyTarget& v) {
    JSON_OPT(j, v, inAreaTime);
    JSON_OPT(j, v, inAreaFullImageUrl);
    JSON_OPT(j, v, outAreaTime);
    JSON_OPT(j, v, outAreaFullImageUrl);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyTarget& v) {
    j["inAreaTime"]          = v.inAreaTime;
    j["inAreaFullImageUrl"]  = v.inAreaFullImageUrl;
    j["outAreaTime"]         = v.outAreaTime;
    j["outAreaFullImageUrl"] = v.outAreaFullImageUrl;
}

void from_json(const nlohmann::json& j, CMsgOnEventsPropertyMatchRst& v) {
    JSON_OPT(j, v, matchId);
    JSON_OPT(j, v, matchDegree);
    JSON_OPT(j, v, groupId);
    JSON_OPT(j, v, groupName);
    JSON_OPT(j, v, baseImageUrl);
}

void to_json(nlohmann::json& j, const CMsgOnEventsPropertyMatchRst& v) {
    j["matchId"]      = v.matchId;
    j["matchDegree"]  = v.matchDegree;
    j["groupId"]      = v.groupId;
    j["groupName"]    = v.groupName;
    j["baseImageUrl"] = v.baseImageUrl;
}

}  // namespace cosmo
