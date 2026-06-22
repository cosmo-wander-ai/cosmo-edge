// ModelDto — Model DTO definitions (extracted from MessageModelHandler.h)

#include "ModelDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Model CRUD and upload serialization (Component in ModelDto_Component.cc)
namespace cosmo::Model {
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
}

void from_json(const nlohmann::json& j, MsgUploadRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, filePath);
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
    j["modelCode"]         = v.modelCode;
    j["modelName"]         = v.modelName;
    j["modelType"]         = v.modelType;
    j["description"]       = v.description;
    j["bmodelFiles"]       = v.bmodelFiles;
    j["vocabFilePath"]     = v.vocabFilePath;
    j["tokenizerFilePath"] = v.tokenizerFilePath;
    j["normalizationMode"] = v.normalizationMode;
    j["colorChannel"]      = v.colorChannel;
}

void from_json(const nlohmann::json& j, MsgAddRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, modelCode);
    JSON_OPT(j, v, modelName);
    JSON_OPT(j, v, modelType);
    JSON_OPT(j, v, description);
    JSON_OPT(j, v, bmodelFiles);
    JSON_OPT(j, v, vocabFilePath);
    JSON_OPT(j, v, tokenizerFilePath);
    JSON_OPT(j, v, normalizationMode);
    JSON_OPT(j, v, colorChannel);
}

void to_json(nlohmann::json& j, const MsgUploadTempRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["contentLength"] = v.contentLength;
    j["fileName"]      = v.fileName;
    j["filePath"]      = v.filePath;
    j["uploadId"]      = v.uploadId;
    j["chunkIndex"]    = v.chunkIndex;
    j["totalChunks"]   = v.totalChunks;
    j["totalSize"]     = v.totalSize;
    j["chunkSize"]     = v.chunkSize;
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
}

void to_json(nlohmann::json& j, const MsgUploadTempSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgUploadTempSend& v) {
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
}

void from_json(const nlohmann::json& j, MsgImportModelRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, filePath);
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
}

void to_json(nlohmann::json& j, const BmodelFileInfo& v) {
    j["role"]     = v.role;
    j["filePath"] = v.filePath;
}

void from_json(const nlohmann::json& j, MsgUploadTempSend::ResData& v) {
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgUploadTempSend::ResData& v) {
    j["filePath"] = v.filePath;
}

void from_json(const nlohmann::json& j, MsgGetConfigSend::ResData& v) {
    JSON_OPT(j, v, configJson);
}

void to_json(nlohmann::json& j, const MsgGetConfigSend::ResData& v) {
    j["configJson"] = v.configJson;
}

}  // namespace cosmo::Model
