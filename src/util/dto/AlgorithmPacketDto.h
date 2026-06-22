// Algorithm packet data types — extracted from flow/action/AlgorithmPacketMng.h
// to break compile-time coupling between service interfaces and flow internals.

#pragma once

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo::service::algorithm {

struct AlgorithmModelInfo {
    std::string modelCode;
    std::string modelName;
    bool bActive{false};
};

struct AlgorithmPacketModel {
    bool bActive{false};
    std::vector<AlgorithmModelInfo> models;
};

struct AlgorithmPacketInfo {
    std::string filePath;
    int algorithmCategory{1};
    int algorithmCode{0};
    AlgorithmPacketModel modelInfo;
    std::string algorithmMetadata;
    cosmo::MsgAlgorithmMetaData metadata;
    std::string algorithmName;
    std::string algorithmProcessdata;
    std::shared_ptr<cosmo::ActionAlg> processdata;
    int algorithmSource{0};
    std::string algorithmUpdateTime;
    int algorithmUsage{0};
    std::string atomicList;
    std::string confVersionId;
    std::string confVersionName;
    std::string configType;
    int64_t createTime{0};
    std::string creator;
    std::string eventType;
    int gafAlgorithmId{0};
    std::string gafAlgorithmName;
    std::string id;
    int isDelete{0};
    std::string packageAlgorithmName;
    int pollingSupport{0};
    std::string remark;
    int status{0};
    std::string supplier;
    int64_t updateTime{0};
    std::string updator;
    int visualized{0};
    friend void to_json(nlohmann::json& j, const AlgorithmPacketInfo& v);
    friend void from_json(const nlohmann::json& j, AlgorithmPacketInfo& v);
};

struct AlgorithmLocalInfo {
    std::string algorithmId;
    std::string algorithmName;
    int algorithmCategory{1};
    std::string remark;
};

// Service-level parameter structure for saving layout
struct LayoutSaveReq {
    std::string confVersionId;
    std::string configVersionName;
    std::string algorithmId;
    std::string algorithmCategory;
    std::string algorithmUsage;
    std::string remark;
    std::string atomicList;
    std::string algorithmProcessdata;
    std::string algorithmMetadata;
    std::string filePath;
};

// Response structs for Layout Detail
struct LayoutDetailVersion {
    std::string id;
    std::string name;
    std::string algorithmCode;
    std::string algorithmMetadata;
    std::string algorithmProcessdata;
    std::string atomicList;
    uint64_t algorithmUpdateTime{0};
};

struct LayoutDetailResult {
    std::string algorithmCode;
    std::string algorithmName;
    std::string algorithmCategory;
    std::string algorithmUsage;
    std::string supplier;
    std::string remark;
    std::string confVersionId;
    std::string algorithmMetadata;
    std::string algorithmProcessdata;
    std::string atomicList;
    std::vector<LayoutDetailVersion> configVersionList;
};

// Response structs for Layout List
struct LayoutListItem {
    std::string algorithmCode;
    std::string algorithmName;
    std::string supplier;
    std::string algorithmUsage;
    std::string description;
};

struct LayoutListResult {
    std::vector<LayoutListItem> list;
};

// Response structs for Layout Export
struct LayoutExportResult {
    std::string filePath;
    std::string fileName;
};

// Response structs for Atomic Action List
struct AtomicAction {
    std::string id;
    std::string name;
    std::string actionName;
    std::string inputParamConfig;
    int actionUsage{0};
    int actionType{0};
};

struct AtomicActionListResult {
    std::vector<AtomicAction> list;
};

}  // namespace cosmo::service::algorithm
