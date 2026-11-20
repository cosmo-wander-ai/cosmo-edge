// Model DTO definitions (extracted from MessageModelHandler.h)

#pragma once

#include <string>
#include <system_error>
#include <vector>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo {
namespace Model {
    // Model page query
    struct MsgPageRecv : public MsgRecvHead {
        std::string modelName;
        std::string modelCode;
        std::string gpuCode;
        int pageNum{1};
        int pageSize{10};
    };

    void to_json(nlohmann::json& j, const MsgPageRecv& v);
    void from_json(const nlohmann::json& j, MsgPageRecv& v);

    struct MsgModelLabel {
        std::string nameCn;
        std::vector<float> threshold;
        std::string label;
        std::string class_name;
        friend void to_json(nlohmann::json& j, const MsgModelLabel& v);
        friend void from_json(const nlohmann::json& j, MsgModelLabel& v);
    };

    struct MsgModel {
        std::string id;
        std::string modelName;
        std::string modelCode;
        std::string gpuCode;
        std::string updateTime;
        std::string version;
        std::string fileAddr;
        std::string remark;
        std::string label;
        int sequenceNumber{0};
        int isDelete{0};
        int algorithmNum{0};
        int status{0};
        std::vector<std::string> algorithmList;
        int inputCount{1};
        std::string inputDim{"1"};
        int outputCount{1};
        std::string outputDim{"5"};
        // False for preset (encrypted, device-bound) models. The console uses
        // this to gate export/delete controls; preset configuration remains editable.
        bool isExportable{true};
        friend void to_json(nlohmann::json& j, const MsgModel& v);
        friend void from_json(const nlohmann::json& j, MsgModel& v);
    };

    struct MsgPageSend : public MsgSendHead {
        struct ResData {
            int total;
            std::vector<MsgModel> rows;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgPageSend& v);
    void from_json(const nlohmann::json& j, MsgPageSend& v);

    // Model upload
    struct MsgUploadRecv : public MsgRecvHead {
        std::string contentLength;
        std::string fileName;
        std::string filePath;
        std::string uploadId;
    };

    void to_json(nlohmann::json& j, const MsgUploadRecv& v);
    void from_json(const nlohmann::json& j, MsgUploadRecv& v);

    struct MsgUploadSend : public MsgSendHead {};

    // Query atomic model list (AIBox platform)
    struct MsgListRecv : public MsgRecvHead {
        std::string modelName;
        std::string modelType;
        std::string filePath;
    };

    void to_json(nlohmann::json& j, const MsgListRecv& v);
    void from_json(const nlohmann::json& j, MsgListRecv& v);

    struct BmodelFileInfo {
        std::string role;
        std::string filePath;
        std::string uploadId;
        friend void to_json(nlohmann::json& j, const BmodelFileInfo& v);
        friend void from_json(const nlohmann::json& j, BmodelFileInfo& v);
    };

    // Add atomic model (AIBox platform)
    struct MsgAddRecv : public MsgRecvHead {
        std::string modelCode;
        std::string modelName;
        std::string modelType;
        std::string description;
        std::vector<BmodelFileInfo> bmodelFiles;
        std::string vocabFilePath;
        std::string vocabUploadId;
        std::string tokenizerFilePath;
        std::string tokenizerUploadId;
        std::string characterTableFilePath;
        std::string characterTableUploadId;
        std::string normalizationMode;
        std::string colorChannel;
    };

    void to_json(nlohmann::json& j, const MsgAddRecv& v);
    void from_json(const nlohmann::json& j, MsgAddRecv& v);

    struct MsgAddSend : public MsgSendHead {};

    struct MsgUploadTempRecv : public MsgRecvHead {
        // Populated by the authenticated multipart parser, not trusted from
        // client-supplied form fields.
        std::string contentLength;
        std::string fileName;
        std::string filePath;
        // Resource-aware chunk upload (/atomic/model/uploadTemp):
        // - uploadId: opaque server-issued ID returned after the first chunk
        // - chunkIndex/totalChunks: Chunk index and total chunks (0-based)
        // - totalSize: Total original file size (bytes)
        // - clientRequestId: stable principal-scoped resume identity
        // Legacy single-upload fields remain accepted during the R1 window.
        std::string uploadId;
        // Multipart form fields are JSON encoded; std::string avoids deserialization mismatch
        std::string chunkIndex;
        std::string totalChunks;
        std::string totalSize;
        std::string chunkSize;
        std::string purpose;
        std::string sha256;
        std::string clientRequestId;
    };

    void to_json(nlohmann::json& j, const MsgUploadTempRecv& v);
    void from_json(const nlohmann::json& j, MsgUploadTempRecv& v);

    struct MsgUploadTempSend : public MsgSendHead {
        struct ResData {
            // R1-only opaque upload:// alias. It is never a server path; new
            // clients use uploadId.
            std::string filePath;
            std::string uploadId;
            std::string nextChunkIndex;
            bool complete{false};
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgUploadTempSend& v);
    void from_json(const nlohmann::json& j, MsgUploadTempSend& v);

    struct MsgCancelUploadRecv : public MsgRecvHead {
        std::string uploadId;
    };

    void to_json(nlohmann::json& j, const MsgCancelUploadRecv& v);
    void from_json(const nlohmann::json& j, MsgCancelUploadRecv& v);

    struct MsgCancelUploadSend : public MsgSendHead {};

    struct MsgUploadCapabilitiesRecv : public MsgRecvHead {};
    void to_json(nlohmann::json& j, const MsgUploadCapabilitiesRecv& v);
    void from_json(const nlohmann::json& j, MsgUploadCapabilitiesRecv& v);

    struct MsgUploadCapabilitiesSend : public MsgSendHead {
        struct ResData {
            std::string maxTotalSize;
            std::string maxChunkSize;
            std::string maxChunks;
            std::string idleTimeoutMs;
            std::string absoluteTimeoutMs;
            std::string availableBytes;
            std::string reserveBytes;
            std::string availableForNewUploadsBytes;
            std::string reservedBySessionsBytes;
            std::string activeSessions;
            std::string maxEncodedImageBytes;
            std::string maxImagePixels;
            bool resumable{true};
            bool persistentAcrossRestart{false};
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgUploadCapabilitiesSend& v);
    void from_json(const nlohmann::json& j, MsgUploadCapabilitiesSend& v);

    struct MsgGetConfigRecv : public MsgRecvHead {
        std::string modelCode;
    };

    void to_json(nlohmann::json& j, const MsgGetConfigRecv& v);
    void from_json(const nlohmann::json& j, MsgGetConfigRecv& v);

    struct MsgGetConfigSend : public MsgSendHead {
        struct ResData {
            std::string configJson;
            bool isExportable{true};        // false for preset (encrypted, device-bound) models
            std::string defaultConfigJson;  // factory snapshot for "restore defaults"; empty if absent
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgGetConfigSend& v);
    void from_json(const nlohmann::json& j, MsgGetConfigSend& v);

    struct MsgSaveConfigRecv : public MsgRecvHead {
        std::string modelCode;
        std::string configJson;
    };

    void to_json(nlohmann::json& j, const MsgSaveConfigRecv& v);
    void from_json(const nlohmann::json& j, MsgSaveConfigRecv& v);

    struct MsgSaveConfigSend : public MsgSendHead {};

    struct MsgExportConfigRecv : public MsgRecvHead {
        std::string modelCode;
        std::string modelName;
    };

    void to_json(nlohmann::json& j, const MsgExportConfigRecv& v);
    void from_json(const nlohmann::json& j, MsgExportConfigRecv& v);

    struct MsgExportConfigSend : public MsgSendHead {
        std::string filePath;
        std::string fileName;
    };

    void to_json(nlohmann::json& j, const MsgExportConfigSend& v);
    void from_json(const nlohmann::json& j, MsgExportConfigSend& v);

    struct MsgImportModelRecv : public MsgRecvHead {
        std::string filePath;
        std::string uploadId;
    };

    void to_json(nlohmann::json& j, const MsgImportModelRecv& v);
    void from_json(const nlohmann::json& j, MsgImportModelRecv& v);

    struct MsgImportModelSend : public MsgSendHead {};

    struct MsgGetModelComponentsRecv : public MsgRecvHead {};

    struct MsgModelComponent {
        std::string id;
        std::string componentName;
        std::string componentType;
        std::string inputParamConfig;
        friend void to_json(nlohmann::json& j, const MsgModelComponent& v);
        friend void from_json(const nlohmann::json& j, MsgModelComponent& v);
    };

    struct MsgGetModelComponentsSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgModelComponent> list;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgGetModelComponentsSend& v);
    void from_json(const nlohmann::json& j, MsgGetModelComponentsSend& v);

    struct MsgDeleteRecv : public MsgRecvHead {
        std::string modelCode;
    };

    void to_json(nlohmann::json& j, const MsgDeleteRecv& v);
    void from_json(const nlohmann::json& j, MsgDeleteRecv& v);

    struct MsgDeleteSend : public MsgSendHead {};

    struct MsgUpdateRecv : public MsgRecvHead {
        std::string modelCode;
        std::string modelName;
        int maxBatch{1};
        std::string description;
    };

    void to_json(nlohmann::json& j, const MsgUpdateRecv& v);
    void from_json(const nlohmann::json& j, MsgUpdateRecv& v);

    struct MsgUpdateSend : public MsgSendHead {};

    struct MsgAtomicModelLabel {
        std::string label;
        std::string nameCN;
        float threshold;
        std::string class_name;
        bool used;
        friend void to_json(nlohmann::json& j, const MsgAtomicModelLabel& v);
        friend void from_json(const nlohmann::json& j, MsgAtomicModelLabel& v);
    };

    struct MsgAtomicModelBehaviorScore {
        std::string class_name;
        std::string nameCN;
        friend void to_json(nlohmann::json& j, const MsgAtomicModelBehaviorScore& v);
        friend void from_json(const nlohmann::json& j, MsgAtomicModelBehaviorScore& v);
    };

    struct MsgAtomicModel {
        std::string atomicCode;
        std::string atomicName;
        std::string label;
        std::vector<MsgAtomicModelLabel> labelList;
        std::vector<MsgAtomicModelBehaviorScore> behaviorScores;
        friend void to_json(nlohmann::json& j, const MsgAtomicModel& v);
        friend void from_json(const nlohmann::json& j, MsgAtomicModel& v);
    };

    struct MsgListSend : public MsgSendHead {
        struct ResData {
            std::vector<MsgAtomicModel> list;
            friend void to_json(nlohmann::json& j, const ResData& v);
            friend void from_json(const nlohmann::json& j, ResData& v);
        } resData;
    };

    void to_json(nlohmann::json& j, const MsgListSend& v);
    void from_json(const nlohmann::json& j, MsgListSend& v);
}  // namespace Model
}  // namespace cosmo
