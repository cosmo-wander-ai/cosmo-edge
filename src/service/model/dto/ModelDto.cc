// ModelDto — Model DTO definitions (extracted from MessageModelHandler.h)

#include "ModelDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Model CRUD and upload serialization (Component in ModelDto_Component.cc)
namespace cosmo::Model {
void from_json(const nlohmann::json& j, MsgQueryGraphModelCandidatesRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, sceneId);
    JSON_OPT(j, v, category);
    JSON_OPT(j, v, targetNodeId);
    JSON_OPT(j, v, algorithmProcessdata);
}
void to_json(nlohmann::json& j, const MsgQueryGraphModelCandidatesRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["sceneId"]              = v.sceneId;
    j["category"]             = v.category;
    j["targetNodeId"]         = v.targetNodeId;
    j["algorithmProcessdata"] = v.algorithmProcessdata;
}
void to_json(nlohmann::json& j, const GraphModelDenial& v) {
    j = {{"modelCode", v.modelCode}, {"reason", v.reason}};
}
void to_json(nlohmann::json& j, const GraphModelItem& v) {
    j = {{"atomicCode", v.atomicCode},
         {"atomicName", v.atomicName},
         {"modelType", v.modelType},
         {"label", v.label}};
}
void to_json(nlohmann::json& j, const MsgQueryGraphModelCandidatesSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = {{"passthrough", v.passthrough},
                    {"allowed", v.allowed},
                    {"allowedModels", v.allowedModels},
                    {"denied", v.denied}};
}
void to_json(nlohmann::json& j, const MsgPageRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelName"] = v.modelName;
    j["modelCode"] = v.modelCode;
    j["gpuCode"]   = v.gpuCode;
    j["pageNum"]   = v.pageNum;
    j["pageSize"]  = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgPageRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelName);
    JSON_OPT(j, v, modelCode);
    JSON_OPT(j, v, gpuCode);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

void to_json(nlohmann::json& j, const MsgPageSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgPageSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgUploadRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["contentLength"] = v.contentLength;
    j["fileName"]      = v.fileName;
    j["filePath"]      = v.filePath;
    j["uploadId"]      = v.uploadId;
}

void from_json(const nlohmann::json& j, MsgUploadRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, uploadId);
}

void to_json(nlohmann::json& j, const MsgListRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelName"] = v.modelName;
    j["modelType"] = v.modelType;
    j["filePath"]  = v.filePath;
}

void from_json(const nlohmann::json& j, MsgListRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelName);
    JSON_OPT(j, v, modelType);
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgAddRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelCode"]              = v.modelCode;
    j["modelName"]              = v.modelName;
    j["modelType"]              = v.modelType;
    j["description"]            = v.description;
    j["bmodelFiles"]            = v.bmodelFiles;
    j["vocabFilePath"]          = v.vocabFilePath;
    j["vocabUploadId"]          = v.vocabUploadId;
    j["tokenizerFilePath"]      = v.tokenizerFilePath;
    j["tokenizerUploadId"]      = v.tokenizerUploadId;
    j["characterTableFilePath"] = v.characterTableFilePath;
    j["characterTableUploadId"] = v.characterTableUploadId;
    j["normalizationMode"]      = v.normalizationMode;
    j["colorChannel"]           = v.colorChannel;
}

void from_json(const nlohmann::json& j, MsgAddRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelCode);
    JSON_OPT(j, v, modelName);
    JSON_OPT(j, v, modelType);
    JSON_OPT(j, v, description);
    JSON_OPT(j, v, bmodelFiles);
    JSON_OPT(j, v, vocabFilePath);
    JSON_OPT(j, v, vocabUploadId);
    JSON_OPT(j, v, tokenizerFilePath);
    JSON_OPT(j, v, tokenizerUploadId);
    JSON_OPT(j, v, characterTableFilePath);
    JSON_OPT(j, v, characterTableUploadId);
    JSON_OPT(j, v, normalizationMode);
    JSON_OPT(j, v, colorChannel);
}

void to_json(nlohmann::json& j, const MsgUploadTempRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["contentLength"]   = v.contentLength;
    j["fileName"]        = v.fileName;
    j["filePath"]        = v.filePath;
    j["uploadId"]        = v.uploadId;
    j["chunkIndex"]      = v.chunkIndex;
    j["totalChunks"]     = v.totalChunks;
    j["totalSize"]       = v.totalSize;
    j["chunkSize"]       = v.chunkSize;
    j["purpose"]         = v.purpose;
    j["sha256"]          = v.sha256;
    j["clientRequestId"] = v.clientRequestId;
}

void from_json(const nlohmann::json& j, MsgUploadTempRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, uploadId);
    JSON_OPT(j, v, chunkIndex);
    JSON_OPT(j, v, totalChunks);
    JSON_OPT(j, v, totalSize);
    JSON_OPT(j, v, chunkSize);
    JSON_OPT(j, v, purpose);
    JSON_OPT(j, v, sha256);
    JSON_OPT(j, v, clientRequestId);
}

void to_json(nlohmann::json& j, const MsgUploadTempSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgUploadTempSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgCancelUploadRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["uploadId"] = v.uploadId;
}

void from_json(const nlohmann::json& j, MsgCancelUploadRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, uploadId);
}

void to_json(nlohmann::json& j, const MsgUploadCapabilitiesRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
}

void from_json(const nlohmann::json& j, MsgUploadCapabilitiesRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
}

void to_json(nlohmann::json& j, const MsgUploadCapabilitiesSend::ResData& v) {
    j["maxTotalSize"]                = v.maxTotalSize;
    j["maxChunkSize"]                = v.maxChunkSize;
    j["maxChunks"]                   = v.maxChunks;
    j["idleTimeoutMs"]               = v.idleTimeoutMs;
    j["absoluteTimeoutMs"]           = v.absoluteTimeoutMs;
    j["availableBytes"]              = v.availableBytes;
    j["reserveBytes"]                = v.reserveBytes;
    j["availableForNewUploadsBytes"] = v.availableForNewUploadsBytes;
    j["reservedBySessionsBytes"]     = v.reservedBySessionsBytes;
    j["activeSessions"]              = v.activeSessions;
    j["maxEncodedImageBytes"]        = v.maxEncodedImageBytes;
    j["maxImagePixels"]              = v.maxImagePixels;
    j["resumable"]                   = v.resumable;
    j["persistentAcrossRestart"]     = v.persistentAcrossRestart;
}

void from_json(const nlohmann::json& j, MsgUploadCapabilitiesSend::ResData& v) {
    JSON_OPT(j, v, maxTotalSize);
    JSON_OPT(j, v, maxChunkSize);
    JSON_OPT(j, v, maxChunks);
    JSON_OPT(j, v, idleTimeoutMs);
    JSON_OPT(j, v, absoluteTimeoutMs);
    JSON_OPT(j, v, availableBytes);
    JSON_OPT(j, v, reserveBytes);
    JSON_OPT(j, v, availableForNewUploadsBytes);
    JSON_OPT(j, v, reservedBySessionsBytes);
    JSON_OPT(j, v, activeSessions);
    JSON_OPT(j, v, maxEncodedImageBytes);
    JSON_OPT(j, v, maxImagePixels);
    JSON_OPT(j, v, resumable);
    JSON_OPT(j, v, persistentAcrossRestart);
}

void to_json(nlohmann::json& j, const MsgUploadCapabilitiesSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgUploadCapabilitiesSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgGetConfigRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelCode"] = v.modelCode;
}

void from_json(const nlohmann::json& j, MsgGetConfigRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelCode);
}

void to_json(nlohmann::json& j, const MsgGetConfigSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgGetConfigSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSaveConfigRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelCode"]  = v.modelCode;
    j["configJson"] = v.configJson;
}

void from_json(const nlohmann::json& j, MsgSaveConfigRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelCode);
    JSON_OPT(j, v, configJson);
}

void to_json(nlohmann::json& j, const MsgExportConfigRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelCode"] = v.modelCode;
    j["modelName"] = v.modelName;
}

void from_json(const nlohmann::json& j, MsgExportConfigRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelCode);
    JSON_OPT(j, v, modelName);
}

void to_json(nlohmann::json& j, const MsgExportConfigSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["filePath"] = v.filePath;
    j["fileName"] = v.fileName;
}

void from_json(const nlohmann::json& j, MsgExportConfigSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, fileName);
}

void to_json(nlohmann::json& j, const MsgImportModelRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["filePath"] = v.filePath;
    j["uploadId"] = v.uploadId;
}

void from_json(const nlohmann::json& j, MsgImportModelRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, uploadId);
}

void to_json(nlohmann::json& j, const MsgGetModelComponentsSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgGetModelComponentsSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelCode"] = v.modelCode;
}

void from_json(const nlohmann::json& j, MsgDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelCode);
}

void to_json(nlohmann::json& j, const MsgUpdateRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["modelCode"]   = v.modelCode;
    j["modelName"]   = v.modelName;
    j["maxBatch"]    = v.maxBatch;
    j["description"] = v.description;
}

void from_json(const nlohmann::json& j, MsgUpdateRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelCode);
    JSON_OPT(j, v, modelName);
    JSON_OPT(j, v, maxBatch);
    JSON_OPT(j, v, description);
}

void to_json(nlohmann::json& j, const MsgListSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgListSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgModelLabel& v) {
    JSON_OPT(j, v, nameCn);
    JSON_OPT(j, v, threshold);
    JSON_OPT(j, v, label);
    JSON_OPT(j, v, class_name);
}

void to_json(nlohmann::json& j, const MsgModelLabel& v) {
    j["nameCn"]     = v.nameCn;
    j["threshold"]  = v.threshold;
    j["label"]      = v.label;
    j["class_name"] = v.class_name;
}

void from_json(const nlohmann::json& j, MsgModel& v) {
    JSON_OPT(j, v, sequenceNumber);
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, modelName);
    JSON_OPT(j, v, modelCode);
    JSON_OPT(j, v, gpuCode);
    JSON_OPT(j, v, updateTime);
    JSON_OPT(j, v, version);
    JSON_OPT(j, v, fileAddr);
    JSON_OPT(j, v, remark);
    JSON_OPT(j, v, isDelete);
    JSON_OPT(j, v, algorithmNum);
    JSON_OPT(j, v, status);
    JSON_OPT(j, v, label);
    JSON_OPT(j, v, algorithmList);
    JSON_OPT(j, v, inputCount);
    JSON_OPT(j, v, inputDim);
    JSON_OPT(j, v, outputCount);
    JSON_OPT(j, v, outputDim);
    JSON_OPT(j, v, isExportable);
}

void to_json(nlohmann::json& j, const MsgModel& v) {
    j["sequenceNumber"] = v.sequenceNumber;
    j["id"]             = v.id;
    j["modelName"]      = v.modelName;
    j["modelCode"]      = v.modelCode;
    j["gpuCode"]        = v.gpuCode;
    j["updateTime"]     = v.updateTime;
    j["version"]        = v.version;
    j["fileAddr"]       = v.fileAddr;
    j["remark"]         = v.remark;
    j["isDelete"]       = v.isDelete;
    j["algorithmNum"]   = v.algorithmNum;
    j["status"]         = v.status;
    j["label"]          = v.label;
    j["algorithmList"]  = v.algorithmList;
    j["inputCount"]     = v.inputCount;
    j["inputDim"]       = v.inputDim;
    j["outputCount"]    = v.outputCount;
    j["outputDim"]      = v.outputDim;
    j["isExportable"]   = v.isExportable;
}

void from_json(const nlohmann::json& j, MsgPageSend::ResData& v) {
    JSON_OPT(j, v, total);
    JSON_OPT(j, v, rows);
}

void to_json(nlohmann::json& j, const MsgPageSend::ResData& v) {
    j["total"] = v.total;
    j["rows"]  = v.rows;
}

void from_json(const nlohmann::json& j, BmodelFileInfo& v) {
    JSON_OPT(j, v, role);
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, uploadId);
}

void to_json(nlohmann::json& j, const BmodelFileInfo& v) {
    j["role"]     = v.role;
    j["filePath"] = v.filePath;
    j["uploadId"] = v.uploadId;
}

void from_json(const nlohmann::json& j, MsgUploadTempSend::ResData& v) {
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, uploadId);
    JSON_OPT(j, v, nextChunkIndex);
    JSON_OPT(j, v, complete);
}

void to_json(nlohmann::json& j, const MsgUploadTempSend::ResData& v) {
    j["filePath"]       = v.filePath;
    j["uploadId"]       = v.uploadId;
    j["nextChunkIndex"] = v.nextChunkIndex;
    j["complete"]       = v.complete;
}

void from_json(const nlohmann::json& j, MsgGetConfigSend::ResData& v) {
    JSON_OPT(j, v, configJson);
    JSON_OPT(j, v, isExportable);
    JSON_OPT(j, v, defaultConfigJson);
}

void to_json(nlohmann::json& j, const MsgGetConfigSend::ResData& v) {
    j["configJson"]        = v.configJson;
    j["isExportable"]      = v.isExportable;
    j["defaultConfigJson"] = v.defaultConfigJson;
}

}  // namespace cosmo::Model
