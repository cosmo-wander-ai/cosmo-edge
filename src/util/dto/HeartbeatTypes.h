// Heartbeat types — CMsgHeartBeatReq, MsgFileServerInfo, AlgActionNodeDurationInfo.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct AlgActionNodeDurationInfo {
    std::string name;          // Node name
    int64_t durationUs{0};     // Period
    int64_t durationAvgUs{0};  // Average processing time in period
    int64_t durationCount{0};  // Processing count in period
    int64_t durationMaxUs{0};  // Maximum processing time in period
    int64_t durationMinUs{0};  // Minimum processing time in period
    int64_t costMaxUs{0};      // Instance max processing time
    int64_t costMinUs{0};      // Instance min processing time
    friend void to_json(nlohmann::json& j, const AlgActionNodeDurationInfo& v);
    friend void from_json(const nlohmann::json& j, AlgActionNodeDurationInfo& v);
};

struct MsgFileServerInfo {
    std::string fileServerUrl;
    std::string user;
    std::string token;
    friend void to_json(nlohmann::json& j, const MsgFileServerInfo& v);
    friend void from_json(const nlohmann::json& j, MsgFileServerInfo& v);
};

struct MsgMemInfo {
    std::string name;
    size_t memTotal{0};      // Total memory in bytes
    size_t memAvailable{0};  // Available memory in bytes
    friend void to_json(nlohmann::json& j, const MsgMemInfo& v);
    friend void from_json(const nlohmann::json& j, MsgMemInfo& v);
};

// Common structure, acts as client heartbeat, referenced by server MsgInfoSend
// Heartbeat request
struct CMsgHeartBeatReq : public MsgRecvHead {
    std::string devId;                // Device ID
    int64_t runtimeDuration{0};       // Runtime duration (seconds)
    int64_t blockedMsg{0};            // Number of blocked/unprocessed tasks
    int hostStatus{0};                // 0: Normal, 400: Abnormal
    std::string needFileServer{"0"};  // Need file server? 0: No, 1: Yes
    std::string heartMode{"1"};       // Health report mode 1: Detailed, 2: Custom
    std::string customScore{
        "1"};                    // Custom resource usage score (0-100). Lower is idle, 100 means no new tasks
    float cpuUsage{0};           // CPU usage, float, 4 decimal places (e.g. 0.8015)
    size_t memTotal{0};          // Total memory in bytes
    size_t memAvailable{0};      // Available memory in bytes
    float gpuUsage{0};           // GPU usage, float, 4 decimal places (e.g. 0.6335)
    size_t gpuMemTotal{0};       // GPU total memory in bytes
    size_t gpuMemAvailable{0};   // GPU available memory in bytes
    size_t gpuModelMemTotal{0};  // GPU model total memory in bytes
    size_t gpuModelMemAvailable{0};         // GPU model available memory in bytes
    size_t gpuPicMemTotal{0};               // GPU picture total memory in bytes
    size_t gpuPicMemAvailable{0};           // GPU picture available memory in bytes
    std::vector<MsgMemInfo> gpuMemDetails;  // GPU memory details
    std::string gpuCapacity;                // GPU load for Ascend
    size_t diskTotal{0};                    // Total disk space in bytes
    size_t diskAvailable{0};                // Available disk space in bytes
    size_t networkUpperrate{0};             // Network uplink in bps
    size_t networkDownwardrate{0};          // Network downlink in bps
    std::string packetDiscardRate{"0.0"};   // Packet loss rate, float, 4 decimal places (e.g. 0.8015)
    uint64_t insertCount{0};                // Inserted node count
    uint64_t processCount{0};               // Processed node count
    uint64_t discardCount{0};               // Discarded node count
    uint64_t insertCountPeriod{0};          // Inserted nodes in period
    uint64_t processCountPeriod{0};         // Processed nodes in period
    uint64_t discardCountPeriod{0};         // Discarded nodes in period
    std::string nodeAlgorithmCheckSum;      // Picture algorithm checksum
    std::vector<AlgActionNodeDurationInfo> nodeDurationInfos;
};

// Inheritance + all-optional fields
void to_json(nlohmann::json& j, const CMsgHeartBeatReq& r);
void from_json(const nlohmann::json& j, CMsgHeartBeatReq& r);

// Heartbeat response
struct CMsgHeartBeatRsp : public MsgSendHead {
    struct {
        std::string user;
        std::string token;
    } resData;
};

}  // namespace cosmo
