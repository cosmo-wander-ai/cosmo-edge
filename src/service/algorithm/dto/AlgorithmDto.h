// Algorithm DTO definitions (extracted from MessageAlgorithmHandler.h)

#pragma once

#include <string>
#include <system_error>
#include <vector>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo {

namespace Algorithm {
    // Algorithm page query
    struct MsgPageRecv : public MsgRecvHead {
        std::string algorithmUsage;
        std::string algorithmName;
        std::string supplier;
        std::string algorithmId;
        std::string algorithmCategory;
        int pageNum{1};
        int pageSize{10};
    };

    void to_json(nlohmann::json& j, const MsgPageRecv& v);
    void from_json(const nlohmann::json& j, MsgPageRecv& v);

    struct MsgAlgorithmVersions {
        std::string gpuCode;
        std::string versionNumber;
        int status{0};
        friend void to_json(nlohmann::json& j, const MsgAlgorithmVersions& v);
        friend void from_json(const nlohmann::json& j, MsgAlgorithmVersions& v);
    };

    struct MsgAlgorithmModelStatus {
        std::string modelCode;
        std::string modelName;
        int modelStatus{0};
    };

    void to_json(nlohmann::json& j, const MsgAlgorithmModelStatus& v);
    void from_json(const nlohmann::json& j, MsgAlgorithmModelStatus& v);

    struct MsgAlgorithm {
        std::string algorithmId;
        std::string algorithmName;
        std::string algorithmCategory;
        std::string algorithmUsage;  // Data source type, consistent with actions.json/algorithmUsage
        std::string categoryName;
        std::string supplier;
        std::string algorithmMetadata;
        std::string remark;
        std::string configType;
        std::string releaseTime;
        std::string confVersionName;
        int versionCount{0};
        int authStatus{0};
        int runningStatus{0};
        MsgAlgorithmVersions versions;
        std::vector<MsgAlgorithmModelStatus> models;

        friend void to_json(nlohmann::json& j, const MsgAlgorithm& v);
        friend void from_json(const nlohmann::json& j, MsgAlgorithm& v);
    };

    // Create task response
    struct MsgPageSend : public MsgSendHead {
        struct ResData {
            int total;
            std::vector<MsgAlgorithm> rows;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgPageSend& v);
    void from_json(const nlohmann::json& j, MsgPageSend& v);

    // Upload algorithm packet
    struct MsgUploadRecv : public MsgRecvHead {
        std::string contentLength;
        std::string fileName;
        std::string filePath;
    };

    void to_json(nlohmann::json& j, const MsgUploadRecv& v);
    void from_json(const nlohmann::json& j, MsgUploadRecv& v);

    // Upload algorithm packet response
    struct MsgUploadSend : public MsgSendHead {};

    // Algorithm edit
    struct MsgUpdateRecv : public MsgRecvHead {
        std::string algorithmId;
        std::string algorithmName;  // Algorithm name
        int algorithmCategory;      // Algorithm category
        std::string remark;         // Algorithm remark
    };

    void to_json(nlohmann::json& j, const MsgUpdateRecv& v);
    void from_json(const nlohmann::json& j, MsgUpdateRecv& v);

    // Algorithm edit response
    struct MsgUpdateSend : public MsgSendHead {};

    // Delete algorithm packet
    struct MsgDeleteRecv : public MsgRecvHead {
        std::string algorithmId;
    };

    void to_json(nlohmann::json& j, const MsgDeleteRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteRecv& v);

    // Delete algorithm packet response
    struct MsgDeleteSend : public MsgSendHead {};

    // Add algorithm (AIBox platform, save to JSON)
    struct MsgAddRecv : public MsgRecvHead {
        std::string algorithmCode;
        std::string algorithmName;
        int algorithmCategory{1};
        int algorithmUsage{0};
        int checkType{0};
        std::string remark;
        std::string eventType;
        std::string filePath;
    };

    void to_json(nlohmann::json& j, const MsgAddRecv& v);
    void from_json(const nlohmann::json& j, MsgAddRecv& v);
    struct MsgAddSend : public MsgSendHead {};

    // Save orchestrated algorithm (AIBox platform)
    struct MsgLayoutSaveRecv : public MsgRecvHead {
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

    void to_json(nlohmann::json& j, const MsgLayoutSaveRecv& v);
    void from_json(const nlohmann::json& j, MsgLayoutSaveRecv& v);
    struct MsgLayoutSaveSend : public MsgSendHead {};

    // Query orchestrated algorithm detail (AIBox platform)
    struct MsgLayoutDetailRecv : public MsgRecvHead {
        std::string id;
        std::string filePath;
    };

    void to_json(nlohmann::json& j, const MsgLayoutDetailRecv& v);
    void from_json(const nlohmann::json& j, MsgLayoutDetailRecv& v);
    struct MsgLayoutDetailVersion {
        std::string id;
        std::string name;
        std::string algorithmCode;
        std::string algorithmMetadata;
        std::string algorithmProcessdata;
        std::string atomicList;
        uint64_t algorithmUpdateTime{0};
        friend void to_json(nlohmann::json& j, const MsgLayoutDetailVersion& v);
        friend void from_json(const nlohmann::json& j, MsgLayoutDetailVersion& v);
    };
    struct MsgLayoutDetailSend : public MsgSendHead {
        struct ResData {
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
            std::vector<MsgLayoutDetailVersion> configVersionList;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgLayoutDetailSend& v);
    void from_json(const nlohmann::json& j, MsgLayoutDetailSend& v);

    // Query orchestrated algorithm list (AIBox platform)
    struct MsgLayoutListRecv : public MsgRecvHead {
        std::string supplier;
        int algorithmUsage{-1};
        std::string filePath;
    };

    void to_json(nlohmann::json& j, const MsgLayoutListRecv& v);
    void from_json(const nlohmann::json& j, MsgLayoutListRecv& v);
    struct MsgLayoutListItem {
        std::string algorithmCode;
        std::string algorithmName;
        std::string supplier;
        std::string algorithmUsage;
        std::string description;
        friend void to_json(nlohmann::json& j, const MsgLayoutListItem& v);
        friend void from_json(const nlohmann::json& j, MsgLayoutListItem& v);
    };
    struct MsgLayoutListSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgLayoutListItem> list;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgLayoutListSend& v);
    void from_json(const nlohmann::json& j, MsgLayoutListSend& v);

    // Export single orchestrated algorithm (AIBox platform)
    struct MsgLayoutExportSingleRecv : public MsgRecvHead {
        std::string algorithmCode;
        std::string algorithmName;
        std::string algorithmCategory;
        std::string algorithmUsage;
        std::string supplier;
        std::string confVersionId;
    };

    void to_json(nlohmann::json& j, const MsgLayoutExportSingleRecv& v);
    void from_json(const nlohmann::json& j, MsgLayoutExportSingleRecv& v);
    struct MsgLayoutExportSingleSend : public MsgSendHead {
        std::string filePath;
        std::string fileName;
    };

    void to_json(nlohmann::json& j, const MsgLayoutExportSingleSend& v);
    void from_json(const nlohmann::json& j, MsgLayoutExportSingleSend& v);

    // Export batch orchestrated algorithms (AIBox platform)
    struct MsgLayoutExportAllRecv : public MsgRecvHead {
        std::string algorithmCode;
        std::string algorithmName;
        std::string algorithmCategory;
        std::string algorithmUsage;
        std::string supplier;
        std::vector<std::string> algorithmIds;  // Optional: Pass algorithm IDs to export selected algorithms
    };

    void to_json(nlohmann::json& j, const MsgLayoutExportAllRecv& v);
    void from_json(const nlohmann::json& j, MsgLayoutExportAllRecv& v);
    struct MsgLayoutExportAllSend : public MsgSendHead {
        std::string filePath;
        std::string fileName;
    };

    void to_json(nlohmann::json& j, const MsgLayoutExportAllSend& v);
    void from_json(const nlohmann::json& j, MsgLayoutExportAllSend& v);

    // Orchestrated action list (AIBox platform)
    struct MsgAtomicActionListRecv : public MsgRecvHead {
        int actionUsage{0};  // 0/-1: All, 1: Video stream analysis, 2: Image analysis
        std::string filePath;
    };

    void to_json(nlohmann::json& j, const MsgAtomicActionListRecv& v);
    void from_json(const nlohmann::json& j, MsgAtomicActionListRecv& v);
    struct MsgAtomicAction {
        std::string id;
        std::string name;
        std::string actionName;
        std::string inputParamConfig;
        int actionUsage{0};
        int actionType{0};
        friend void to_json(nlohmann::json& j, const MsgAtomicAction& v);
        friend void from_json(const nlohmann::json& j, MsgAtomicAction& v);
    };
    struct MsgAtomicActionListSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgAtomicAction> list;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgAtomicActionListSend& v);
    void from_json(const nlohmann::json& j, MsgAtomicActionListSend& v);

    struct MsgPassFlowListRecv : public MsgRecvHead {};

    // Passenger flow list response
    struct MsgPassFlowListSend : public MsgSendHead {
        struct MsgPassFlowUnit {
            std::string algorithmId;
            std::string algorithmName;
            friend void to_json(nlohmann::json& j, const MsgPassFlowUnit& v);
            friend void from_json(const nlohmann::json& j, MsgPassFlowUnit& v);
        };

        struct ResData {
            std::vector<MsgPassFlowUnit> list;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgPassFlowListSend& v);
    void from_json(const nlohmann::json& j, MsgPassFlowListSend& v);
}  // namespace Algorithm
}  // namespace cosmo
