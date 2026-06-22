// TaskMsgTypes — Task Msg Types implementation.

#include <nlohmann/json.hpp>

#include "HeartbeatTypes.h"
#include "TaskAreaTypes.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Heartbeat and common data type serialization
// (TaskCreate in TaskMsgTypes_TaskCreate.cc, PicTask in TaskMsgTypes_PicTask.cc)
namespace cosmo {

void to_json(nlohmann::json& j, const MsgFileServerInfo& v) {
    j["fileServerUrl"] = v.fileServerUrl;
    j["user"]          = v.user;
    j["token"]         = v.token;
}
void from_json(const nlohmann::json& j, MsgFileServerInfo& v) {
    JSON_OPT(j, v, fileServerUrl);
    JSON_OPT(j, v, user);
    JSON_OPT(j, v, token);
}

void to_json(nlohmann::json& j, const CMsgHeartBeatReq& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["devId"]                 = r.devId;
    j["runtimeDuration"]       = r.runtimeDuration;
    j["blockedMsg"]            = r.blockedMsg;
    j["hostStatus"]            = r.hostStatus;
    j["needFileServer"]        = r.needFileServer;
    j["heartMode"]             = r.heartMode;
    j["customScore"]           = r.customScore;
    j["cpuUsage"]              = r.cpuUsage;
    j["memTotal"]              = r.memTotal;
    j["memAvailable"]          = r.memAvailable;
    j["gpuUsage"]              = r.gpuUsage;
    j["gpuMemTotal"]           = r.gpuMemTotal;
    j["gpuMemAvailable"]       = r.gpuMemAvailable;
    j["gpuCapacity"]           = r.gpuCapacity;
    j["gpuModelMemTotal"]      = r.gpuModelMemTotal;
    j["gpuModelMemAvailable"]  = r.gpuModelMemAvailable;
    j["gpuPicMemTotal"]        = r.gpuPicMemTotal;
    j["gpuPicMemAvailable"]    = r.gpuPicMemAvailable;
    j["gpuMemDetails"]         = r.gpuMemDetails;
    j["diskTotal"]             = r.diskTotal;
    j["diskAvailable"]         = r.diskAvailable;
    j["networkUpperrate"]      = r.networkUpperrate;
    j["networkDownwardrate"]   = r.networkDownwardrate;
    j["packetDiscardRate"]     = r.packetDiscardRate;
    j["insertCount"]           = r.insertCount;
    j["processCount"]          = r.processCount;
    j["discardCount"]          = r.discardCount;
    j["insertCountPeriod"]     = r.insertCountPeriod;
    j["processCountPeriod"]    = r.processCountPeriod;
    j["discardCountPeriod"]    = r.discardCountPeriod;
    j["nodeAlgorithmCheckSum"] = r.nodeAlgorithmCheckSum;
    j["nodeDurationInfos"]     = r.nodeDurationInfos;
}

void from_json(const nlohmann::json& j, CMsgHeartBeatReq& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    JSON_OPT(j, r, devId);
    JSON_OPT(j, r, runtimeDuration);
    JSON_OPT(j, r, blockedMsg);
    JSON_OPT(j, r, hostStatus);
    JSON_OPT(j, r, needFileServer);
    JSON_OPT(j, r, heartMode);
    JSON_OPT(j, r, customScore);
    JSON_OPT(j, r, cpuUsage);
    JSON_OPT(j, r, memTotal);
    JSON_OPT(j, r, memAvailable);
    JSON_OPT(j, r, gpuUsage);
    JSON_OPT(j, r, gpuMemTotal);
    JSON_OPT(j, r, gpuMemAvailable);
    JSON_OPT(j, r, gpuCapacity);
    JSON_OPT(j, r, gpuModelMemTotal);
    JSON_OPT(j, r, gpuModelMemAvailable);
    JSON_OPT(j, r, gpuPicMemTotal);
    JSON_OPT(j, r, gpuPicMemAvailable);
    JSON_OPT(j, r, gpuMemDetails);
    JSON_OPT(j, r, diskTotal);
    JSON_OPT(j, r, diskAvailable);
    JSON_OPT(j, r, networkUpperrate);
    JSON_OPT(j, r, networkDownwardrate);
    JSON_OPT(j, r, packetDiscardRate);
    JSON_OPT(j, r, insertCount);
    JSON_OPT(j, r, processCount);
    JSON_OPT(j, r, discardCount);
    JSON_OPT(j, r, insertCountPeriod);
    JSON_OPT(j, r, processCountPeriod);
    JSON_OPT(j, r, discardCountPeriod);
    JSON_OPT(j, r, nodeAlgorithmCheckSum);
    JSON_OPT(j, r, nodeDurationInfos);
}

void to_json(nlohmann::json& j, const MsgTaskArea& a) {
    j["areaId"]          = a.areaId;
    j["name"]            = a.name;
    j["points"]          = a.points;
    j["associatedAreas"] = a.associatedAreas;
    j["linePoints"]      = a.linePoints;
    j["params"]          = a.params;
    if (ValidateRetroDirect(static_cast<int>(a.iretroDirect))) {
        j["retroDirect"] = a.retroDirect;
    }
}

void from_json(const nlohmann::json& j, MsgTaskArea& a) {
    JSON_OPT(j, a, areaId);
    JSON_OPT(j, a, name);
    JSON_OPT(j, a, points);
    JSON_OPT(j, a, associatedAreas);
    JSON_OPT(j, a, linePoints);
    JSON_OPT(j, a, params);
    JSON_OPT(j, a, retroDirect);
}

void to_json(nlohmann::json& j, const MsgRunTime& r) {
    j = nlohmann::json{{"timeBegin", r.timeBegin}, {"timeEnd", r.timeEnd}};
}

void from_json(const nlohmann::json& j, MsgRunTime& r) {
    j.at("timeBegin").get_to(r.timeBegin);
    j.at("timeEnd").get_to(r.timeEnd);
}

void to_json(nlohmann::json& j, const MsgTarget& t) {
    j["trackId"]           = t.trackId;
    j["bFilter"]           = t.bFilter;
    j["bLogicResult"]      = t.bLogicResult;
    j["filterDesc"]        = t.filterDesc;
    j["hwRatio"]           = t.hwRatio;
    j["hwRatioVariation"]  = t.hwRatioVariation;
    j["trackStatus"]       = t.trackStatus;
    j["motionStatus"]      = t.motionStatus;
    j["shapeChangeStatus"] = t.shapeChangeStatus;
    j["box"]               = t.box;
    j["aiBox"]             = t.aiBox;
    j["confidence"]        = t.confidence;
    j["attrs"]             = t.attrs;
    j["areas"]             = t.areas;
    j["shiledAreas"]       = t.shiledAreas;
    if (t.bHaveMatchInfo)
        j["matchInfo"] = t.matchInfo;
    if (!t.groupEls.empty())
        j["groupEls"] = t.groupEls;
}

void from_json(const nlohmann::json& j, MsgTarget& t) {
    JSON_OPT(j, t, trackId);
    JSON_OPT(j, t, bFilter);
    JSON_OPT(j, t, bLogicResult);
    JSON_OPT(j, t, filterDesc);
    JSON_OPT(j, t, hwRatio);
    JSON_OPT(j, t, hwRatioVariation);
    JSON_OPT(j, t, trackStatus);
    JSON_OPT(j, t, motionStatus);
    JSON_OPT(j, t, shapeChangeStatus);
    JSON_OPT(j, t, box);
    JSON_OPT(j, t, aiBox);
    JSON_OPT(j, t, confidence);
    JSON_OPT(j, t, attrs);
    JSON_OPT(j, t, areas);
    JSON_OPT(j, t, shiledAreas);
    if (auto it = j.find("matchInfo"); it != j.end() && !it->is_null()) {
        t.bHaveMatchInfo = true;
        it->get_to(t.matchInfo);
    }
    JSON_OPT(j, t, groupEls);
}

void from_json(const nlohmann::json& j, AlgActionNodeDurationInfo& v) {
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, durationUs);
    JSON_OPT(j, v, durationAvgUs);
    JSON_OPT(j, v, durationCount);
    JSON_OPT(j, v, durationMaxUs);
    JSON_OPT(j, v, durationMinUs);
    JSON_OPT(j, v, costMaxUs);
    JSON_OPT(j, v, costMinUs);
}

void to_json(nlohmann::json& j, const AlgActionNodeDurationInfo& v) {
    j["name"]          = v.name;
    j["durationUs"]    = v.durationUs;
    j["durationAvgUs"] = v.durationAvgUs;
    j["durationCount"] = v.durationCount;
    j["durationMaxUs"] = v.durationMaxUs;
    j["durationMinUs"] = v.durationMinUs;
    j["costMaxUs"]     = v.costMaxUs;
    j["costMinUs"]     = v.costMinUs;
}

void from_json(const nlohmann::json& j, MsgMemInfo& v) {
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, memTotal);
    JSON_OPT(j, v, memAvailable);
}

void to_json(nlohmann::json& j, const MsgMemInfo& v) {
    j["name"]         = v.name;
    j["memTotal"]     = v.memTotal;
    j["memAvailable"] = v.memAvailable;
}

}  // namespace cosmo
