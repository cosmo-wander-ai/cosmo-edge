// SystemMsgTypes — System info types — MsgMemoryInfo, MsgGpuInfo, MsgDiskInfo, MsgNetInfo, MsgHw...

#include "SystemMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void from_json(const nlohmann::json& j, DeviceInfo& v) {
    JSON_OPT(j, v, key);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, value);
}

void to_json(nlohmann::json& j, const DeviceInfo& v) {
    j["key"]   = v.key;
    j["name"]  = v.name;
    j["value"] = v.value;
}

void from_json(const nlohmann::json& j, MsgMemoryInfo& v) {
    JSON_OPT(j, v, memtotal);
    JSON_OPT(j, v, memavailable);
}

void to_json(nlohmann::json& j, const MsgMemoryInfo& v) {
    j["memtotal"]     = v.memtotal;
    j["memavailable"] = v.memavailable;
}

void from_json(const nlohmann::json& j, MsgGpuDevUsage& v) {
    JSON_OPT(j, v, gpuusage);
    JSON_OPT(j, v, gpumemusage);
    JSON_OPT(j, v, gpumem);
    JSON_OPT(j, v, gpumemtotal);
    JSON_OPT(j, v, gpumemavailable);
}

void to_json(nlohmann::json& j, const MsgGpuDevUsage& v) {
    j["gpuusage"]        = v.gpuusage;
    j["gpumemusage"]     = v.gpumemusage;
    j["gpumem"]          = v.gpumem;
    j["gpumemtotal"]     = v.gpumemtotal;
    j["gpumemavailable"] = v.gpumemavailable;
}

void from_json(const nlohmann::json& j, MsgGpuInfo& v) {
    JSON_OPT(j, v, gpuusage);
    JSON_OPT(j, v, gpumemusage);
    JSON_OPT(j, v, gpumemtotal);
    JSON_OPT(j, v, gpumemavailable);
    JSON_OPT(j, v, gpudevusage);
    JSON_OPT(j, v, gpuCapacity);
}

void to_json(nlohmann::json& j, const MsgGpuInfo& v) {
    j["gpuusage"]        = v.gpuusage;
    j["gpumemusage"]     = v.gpumemusage;
    j["gpumemtotal"]     = v.gpumemtotal;
    j["gpumemavailable"] = v.gpumemavailable;
    j["gpudevusage"]     = v.gpudevusage;
    j["gpuCapacity"]     = v.gpuCapacity;
}

void from_json(const nlohmann::json& j, MsgDiskInfo& v) {
    JSON_OPT(j, v, disktotal);
    JSON_OPT(j, v, diskavailable);
}

void to_json(nlohmann::json& j, const MsgDiskInfo& v) {
    j["disktotal"]     = v.disktotal;
    j["diskavailable"] = v.diskavailable;
}

void from_json(const nlohmann::json& j, MsgNetInfo& v) {
    JSON_OPT(j, v, networkupperrate);
    JSON_OPT(j, v, networkdownwardrate);
}

void to_json(nlohmann::json& j, const MsgNetInfo& v) {
    j["networkupperrate"]    = v.networkupperrate;
    j["networkdownwardrate"] = v.networkdownwardrate;
}

void from_json(const nlohmann::json& j, MsgHwInfo& v) {
    JSON_OPT(j, v, cpuusage);
    JSON_OPT(j, v, memoryinfo);
    JSON_OPT(j, v, gpuinfo);
    JSON_OPT(j, v, diskinfo);
    JSON_OPT(j, v, netinfo);
}

void to_json(nlohmann::json& j, const MsgHwInfo& v) {
    j["cpuusage"]   = v.cpuusage;
    j["memoryinfo"] = v.memoryinfo;
    j["gpuinfo"]    = v.gpuinfo;
    j["diskinfo"]   = v.diskinfo;
    j["netinfo"]    = v.netinfo;
}

void from_json(const nlohmann::json& j, ActionStatus& v) {
    JSON_OPT(j, v, statusCode);
    JSON_OPT(j, v, statusDesc);
    JSON_OPT(j, v, actionId);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, holdCount);
    JSON_OPT(j, v, alarmCount);
    JSON_OPT(j, v, insertCount);
    JSON_OPT(j, v, processCount);
    JSON_OPT(j, v, discardCount);
    JSON_OPT(j, v, periodMs);
    JSON_OPT(j, v, insertCountPeriod);
    JSON_OPT(j, v, processCountPeriod);
    JSON_OPT(j, v, discardCountPeriod);
}

void to_json(nlohmann::json& j, const ActionStatus& v) {
    j["statusCode"]         = v.statusCode;
    j["statusDesc"]         = v.statusDesc;
    j["actionId"]           = v.actionId;
    j["name"]               = v.name;
    j["holdCount"]          = v.holdCount;
    j["alarmCount"]         = v.alarmCount;
    j["insertCount"]        = v.insertCount;
    j["processCount"]       = v.processCount;
    j["discardCount"]       = v.discardCount;
    j["periodMs"]           = v.periodMs;
    j["insertCountPeriod"]  = v.insertCountPeriod;
    j["processCountPeriod"] = v.processCountPeriod;
    j["discardCountPeriod"] = v.discardCountPeriod;
}

}  // namespace cosmo
