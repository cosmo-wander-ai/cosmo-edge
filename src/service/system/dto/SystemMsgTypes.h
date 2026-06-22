// System info types — MsgMemoryInfo, MsgGpuInfo, MsgDiskInfo, MsgNetInfo, MsgHwInfo.
// Modular DTO header.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct DeviceInfo {
    std::string key;
    std::string name;
    std::string value;
    friend void to_json(nlohmann::json& j, const DeviceInfo& v);
    friend void from_json(const nlohmann::json& j, DeviceInfo& v);
};

struct MsgMemoryInfo {
    int64_t memtotal{-1};
    int64_t memavailable{-1};
    friend void to_json(nlohmann::json& j, const MsgMemoryInfo& v);
    friend void from_json(const nlohmann::json& j, MsgMemoryInfo& v);
};

struct MsgGpuDevUsage {
    double gpuusage{0.0};
    double gpumemusage{0.0};
    int64_t gpumem{0};
    int64_t gpumemtotal{0};
    int64_t gpumemavailable{0};
    friend void to_json(nlohmann::json& j, const MsgGpuDevUsage& v);
    friend void from_json(const nlohmann::json& j, MsgGpuDevUsage& v);
};

struct MsgGpuInfo {
    double gpuusage{0.0};
    double gpumemusage{0.0};
    int64_t gpumemtotal{0};
    int64_t gpumemavailable{0};
    std::string gpuCapacity;
    std::vector<MsgGpuDevUsage> gpudevusage;
    friend void to_json(nlohmann::json& j, const MsgGpuInfo& v);
    friend void from_json(const nlohmann::json& j, MsgGpuInfo& v);
};

struct MsgDiskInfo {
    int64_t disktotal{-1};
    int64_t diskavailable{-1};
    friend void to_json(nlohmann::json& j, const MsgDiskInfo& v);
    friend void from_json(const nlohmann::json& j, MsgDiskInfo& v);
};

struct MsgNetInfo {
    int64_t networkupperrate{0};
    int64_t networkdownwardrate{0};
    friend void to_json(nlohmann::json& j, const MsgNetInfo& v);
    friend void from_json(const nlohmann::json& j, MsgNetInfo& v);
};
struct MsgHwInfo {
    double cpuusage;
    MsgMemoryInfo memoryinfo;
    MsgGpuInfo gpuinfo;
    MsgDiskInfo diskinfo;
    MsgNetInfo netinfo;
    friend void to_json(nlohmann::json& j, const MsgHwInfo& v);
    friend void from_json(const nlohmann::json& j, MsgHwInfo& v);
};

struct ActionStatus {
    std::string statusCode;
    std::string statusDesc;
    std::string actionId;
    std::string name;

    uint64_t holdCount{0};  // Current queue size (pending packets)

    size_t alarmCount{0};      // Alarm count (alarm nodes only)
    uint64_t insertCount{0};   // Total inserted packets
    uint64_t processCount{0};  // Total processed packets
    uint64_t discardCount{0};  // Total discarded packets

    int64_t periodMs{0};
    uint64_t insertCountPeriod{0};   // Inserted packets in current period
    uint64_t processCountPeriod{0};  // Processed packets in current period
    uint64_t discardCountPeriod{0};  // Discarded packets in current period
    friend void to_json(nlohmann::json& j, const ActionStatus& v);
    friend void from_json(const nlohmann::json& j, ActionStatus& v);
};
using ActionStatusPtr = std::shared_ptr<ActionStatus>;

}  // namespace cosmo
