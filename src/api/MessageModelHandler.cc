// MessageModelHandler — thin proxy layer for model management API.
// All business logic is delegated to IModelService.

#include "api/MessageModelHandler.h"

#include "service/model/IModelService.h"
#include "util/ErrorCode.h"

namespace cosmo {
MessageModelHandler::MessageModelHandler(service::IModelService& model_service)
    : model_service_(model_service) {}

// Query model list (paginated)
Model::MsgPageSend MessageModelHandler::Handle(Model::MsgPageRecv&& data, std::error_condition& errc) const {
    Model::MsgPageSend retData{};
    model_service_.QueryModels(data.modelName, data.modelCode, data.pageNum, data.pageSize,
                               retData.resData.total, retData.resData.rows);
    errc = util::ErrorEnum::Success;
    return retData;
}

// Upload model (legacy)
Model::MsgUploadSend MessageModelHandler::Handle(Model::MsgUploadRecv&& data,
                                                 std::error_condition& errc) const {
    Model::MsgUploadSend retData{};
    errc = model_service_.ModelAdd(data.filePath);
    return retData;
}

// Query atomic model list
Model::MsgListSend MessageModelHandler::Handle(Model::MsgListRecv&& data, std::error_condition& errc) const {
    Model::MsgListSend retData{};
    retData.resData.list = model_service_.QueryAtomicModels(data.modelName, data.modelType, data.filePath);
    errc                 = util::ErrorEnum::Success;
    return retData;
}

// Add atomic model
Model::MsgAddSend MessageModelHandler::Handle(Model::MsgAddRecv&& data, std::error_condition& errc) const {
    Model::MsgAddSend retData{};
    errc = model_service_.AddAtomicModel(data.modelCode, data.modelName, data.modelType, data.description,
                                         data.bmodelFiles, data.vocabFilePath, data.tokenizerFilePath,
                                         data.normalizationMode, data.colorChannel);
    return retData;
}

// Upload temp file
Model::MsgUploadTempSend MessageModelHandler::Handle(Model::MsgUploadTempRecv&& data,
                                                     std::error_condition& errc) const {
    Model::MsgUploadTempSend retData{};
    errc = model_service_.UploadTempFile(data.filePath, data.fileName, data.contentLength, data.uploadId,
                                         data.chunkIndex, data.totalChunks, retData.resData.filePath);
    return retData;
}

// Get model config
Model::MsgGetConfigSend MessageModelHandler::Handle(Model::MsgGetConfigRecv&& data,
                                                    std::error_condition& errc) const {
    Model::MsgGetConfigSend retData{};
    errc = model_service_.GetModelConfig(data.modelCode, retData.resData.configJson);
    return retData;
}

// Save model config
Model::MsgSaveConfigSend MessageModelHandler::Handle(Model::MsgSaveConfigRecv&& data,
                                                     std::error_condition& errc) const {
    Model::MsgSaveConfigSend retData{};
    errc = model_service_.SaveModelConfig(data.modelCode, data.configJson);
    return retData;
}

// Get model components
Model::MsgGetModelComponentsSend MessageModelHandler::Handle(Model::MsgGetModelComponentsRecv&& /*data*/,
                                                             std::error_condition& errc) const {
    Model::MsgGetModelComponentsSend retData{};
    retData.resData.list = model_service_.GetModelComponents();
    errc                 = util::ErrorEnum::Success;
    return retData;
}

// Delete model
Model::MsgDeleteSend MessageModelHandler::Handle(Model::MsgDeleteRecv&& data,
                                                 std::error_condition& errc) const {
    Model::MsgDeleteSend retData{};
    errc = model_service_.DeleteModel(data.modelCode);
    return retData;
}

// Update model
Model::MsgUpdateSend MessageModelHandler::Handle(Model::MsgUpdateRecv&& data,
                                                 std::error_condition& errc) const {
    Model::MsgUpdateSend retData{};
    errc = model_service_.UpdateModel(data.modelCode, data.modelName, data.maxBatch, data.description);
    return retData;
}

// Export model config
Model::MsgExportConfigSend MessageModelHandler::Handle(Model::MsgExportConfigRecv&& data,
                                                       std::error_condition& errc) const {
    Model::MsgExportConfigSend retData{};
    errc =
        model_service_.ExportModelConfig(data.modelCode, data.modelName, retData.filePath, retData.fileName);
    return retData;
}

// Import model from tar.gz archive
Model::MsgImportModelSend MessageModelHandler::Handle(Model::MsgImportModelRecv&& data,
                                                      std::error_condition& errc) const {
    Model::MsgImportModelSend retData{};
    errc = model_service_.ImportModel(data.filePath);
    return retData;
}
}  // namespace cosmo
