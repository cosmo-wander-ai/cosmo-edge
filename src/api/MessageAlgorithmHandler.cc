// MessageAlgorithmHandler — Route requests to appropriate handler methods based on message type

#include "api/MessageAlgorithmHandler.h"

#include "service/algorithm/AlgorithmMapper.h"
#include "service/algorithm/IAlgorithmCrud.h"
#include "service/algorithm/IAlgorithmLayout.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/task/ITaskQuery.h"
#include "util/DateTimeFormat.h"
#include "util/ErrorCode.h"
#include "util/Log.h"

namespace cosmo {

MessageAlgorithmHandler::MessageAlgorithmHandler(service::IAlgorithmQuery& algorithm_query,
                                                 service::IAlgorithmCrud& algorithm_crud,
                                                 service::IAlgorithmLayout& algorithm_layout,
                                                 service::ICameraTaskConfig& camera_task_config,
                                                 service::ITaskQuery& task_query)
    : algorithm_query_(algorithm_query),
      algorithm_crud_(algorithm_crud),
      algorithm_layout_(algorithm_layout),
      camera_task_config_(camera_task_config),
      task_query_(task_query) {}

// Query algorithm list
Algorithm::MsgPageSend MessageAlgorithmHandler::Handle(Algorithm::MsgPageRecv&& data,
                                                       std::error_condition& /*errc*/) const {
    Algorithm::MsgPageSend retData{};

    size_t total = 0;
    auto infos =
        algorithm_query_.Query(data.algorithmUsage, data.algorithmName, data.supplier, data.algorithmId,
                               data.algorithmCategory, data.pageNum, data.pageSize, total);
    for (const auto& info : infos) {
        Algorithm::MsgAlgorithm algorithm;

        algorithm.algorithmId       = info.id;
        algorithm.algorithmName     = info.algorithmName;
        algorithm.algorithmCategory = std::to_string(info.algorithmCategory);
        algorithm.algorithmUsage    = std::to_string(info.algorithmUsage);
        algorithm.confVersionName   = info.confVersionName;
        algorithm.supplier          = info.supplier;
        algorithm.algorithmMetadata = info.algorithmMetadata;
        auto date                   = util::GetDateTime(info.updateTime);
        algorithm.releaseTime       = date.Date().ToYMD() + " " + date.Time().ToHMS();
        algorithm.runningStatus     = task_query_.GetAlgorithmCount(algorithm.algorithmId);

        for (const auto& model : info.modelInfo.models) {
            Algorithm::MsgAlgorithmModelStatus modelStatus;
            modelStatus.modelCode   = model.modelCode;
            modelStatus.modelName   = model.modelName;
            modelStatus.modelStatus = model.bActive;
            algorithm.models.push_back(modelStatus);
        }
        algorithm.remark = info.remark;
        retData.resData.rows.push_back(algorithm);
    }
    retData.resData.total = static_cast<int>(total);

    return retData;
}

// Upload algorithm package
Algorithm::MsgUploadSend MessageAlgorithmHandler::Handle(Algorithm::MsgUploadRecv&& data,
                                                         std::error_condition& errc) const {
    Algorithm::MsgUploadSend retData{};
    errc = algorithm_crud_.Add(data.filePath);
    return retData;
}

// Algorithm edit
Algorithm::MsgUpdateSend MessageAlgorithmHandler::Handle(Algorithm::MsgUpdateRecv&& data,
                                                         std::error_condition& errc) const {
    Algorithm::MsgUpdateSend retData{};
    errc = algorithm_crud_.Update(data.algorithmId, data.algorithmName, data.algorithmCategory, data.remark);
    if (errc == util::ErrorEnum::Success) {
        camera_task_config_.NotifyAlgorithmsChanged({data.algorithmId}, false);
    }
    return retData;
}

// Algorithm delete
Algorithm::MsgDeleteSend MessageAlgorithmHandler::Handle(Algorithm::MsgDeleteRecv&& data,
                                                         std::error_condition& errc) const {
    Algorithm::MsgDeleteSend retData{};
    // Check if algorithm is being used by video access tasks
    if (camera_task_config_.IsAlgorithmInUse(data.algorithmId)) {
        errc = util::ErrorEnum::AlgorithmInUse;
        return retData;
    }
    errc = algorithm_crud_.Delete(data.algorithmId);
    if (errc == util::ErrorEnum::Success) {
        camera_task_config_.NotifyAlgorithmsDeleted({data.algorithmId});
    }
    return retData;
}

// Add new algorithm (AIBox platform, save to JSON)
Algorithm::MsgAddSend MessageAlgorithmHandler::Handle(Algorithm::MsgAddRecv&& data,
                                                      std::error_condition& errc) const {
    Algorithm::MsgAddSend retData{};
    errc = algorithm_crud_.AddFromJson(data.algorithmName, data.algorithmCategory, data.algorithmUsage,
                                       data.remark, data.eventType, data.filePath);
    return retData;
}

// Save orchestration algorithm (AIBox platform)
Algorithm::MsgLayoutSaveSend MessageAlgorithmHandler::Handle(Algorithm::MsgLayoutSaveRecv&& data,
                                                             std::error_condition& errc) const {
    Algorithm::MsgLayoutSaveSend retData{};
    service::algorithm::LayoutSaveReq req;
    req.confVersionId        = data.confVersionId;
    req.configVersionName    = data.configVersionName;
    req.algorithmId          = data.algorithmId;
    req.algorithmCategory    = data.algorithmCategory;
    req.algorithmUsage       = data.algorithmUsage;
    req.remark               = data.remark;
    req.atomicList           = data.atomicList;
    req.algorithmProcessdata = data.algorithmProcessdata;
    req.algorithmMetadata    = data.algorithmMetadata;
    req.filePath             = data.filePath;

    errc = algorithm_layout_.LayoutSave(req);
    if (errc == util::ErrorEnum::Success) {
        camera_task_config_.NotifyAlgorithmsChanged({data.algorithmId}, true);
    }
    return retData;
}

// Query orchestration algorithm details (AIBox platform)
Algorithm::MsgLayoutDetailSend MessageAlgorithmHandler::Handle(Algorithm::MsgLayoutDetailRecv&& data,
                                                               std::error_condition& errc) const {
    Algorithm::MsgLayoutDetailSend retData{};
    service::algorithm::LayoutDetailResult res;
    errc = algorithm_layout_.GetLayoutDetail(data.id, data.filePath, res);
    if (errc == util::ErrorEnum::Success) {
        service::algorithm::ToWire(res, retData.resData);
    }
    return retData;
}

// Query orchestration algorithm list (AIBox platform)
Algorithm::MsgLayoutListSend MessageAlgorithmHandler::Handle(Algorithm::MsgLayoutListRecv&& data,
                                                             std::error_condition& errc) const {
    Algorithm::MsgLayoutListSend retData{};
    service::algorithm::LayoutListResult res;
    errc = algorithm_layout_.GetLayoutList(data.supplier, data.algorithmUsage, data.filePath, res);
    if (errc == util::ErrorEnum::Success) {
        service::algorithm::ToWire(res, retData.resData.list);
    }
    return retData;
}

// Export single orchestration algorithm (AIBox platform)
Algorithm::MsgLayoutExportSingleSend MessageAlgorithmHandler::Handle(
    Algorithm::MsgLayoutExportSingleRecv&& data, std::error_condition& errc) const {
    Algorithm::MsgLayoutExportSingleSend retData{};
    service::algorithm::LayoutExportResult res;
    errc =
        algorithm_layout_.LayoutExportSingle(data.algorithmCode, data.algorithmName, data.algorithmCategory,
                                             data.supplier, data.confVersionId, res);
    if (errc == util::ErrorEnum::Success) {
        retData.filePath = res.filePath;
        retData.fileName = res.fileName;
    }
    return retData;
}

// Batch export orchestration algorithms (AIBox platform)
Algorithm::MsgLayoutExportAllSend MessageAlgorithmHandler::Handle(Algorithm::MsgLayoutExportAllRecv&& data,
                                                                  std::error_condition& errc) const {
    Algorithm::MsgLayoutExportAllSend retData{};
    service::algorithm::LayoutExportResult res;
    errc = algorithm_layout_.LayoutExportAll(data.algorithmName, data.supplier, data.algorithmUsage,
                                             data.algorithmCategory, data.algorithmIds, res);
    if (errc == util::ErrorEnum::Success) {
        retData.filePath = res.filePath;
        retData.fileName = res.fileName;
    }
    return retData;
}

// Query orchestration action list (AIBox platform)
Algorithm::MsgAtomicActionListSend MessageAlgorithmHandler::Handle(Algorithm::MsgAtomicActionListRecv&& data,
                                                                   std::error_condition& errc) const {
    Algorithm::MsgAtomicActionListSend retData{};
    service::algorithm::AtomicActionListResult res;
    errc = algorithm_layout_.GetAtomicActionList(data.actionUsage, data.filePath, res);
    if (errc == util::ErrorEnum::Success) {
        service::algorithm::ToWire(res, retData.resData.list);
    }
    return retData;
}

// Query passenger/vehicle flow algorithm list
Algorithm::MsgPassFlowListSend MessageAlgorithmHandler::Handle(Algorithm::MsgPassFlowListRecv&& /*data*/,
                                                               std::error_condition& /*errc*/) const {
    Algorithm::MsgPassFlowListSend retData{};

    auto infos = algorithm_query_.GetPassFlowAlgorithms();
    for (const auto& info : infos) {
        Algorithm::MsgPassFlowListSend::MsgPassFlowUnit unit;
        unit.algorithmId   = info.algorithmId;
        unit.algorithmName = info.algorithmName;
        retData.resData.list.push_back(unit);
    }

    return retData;
}
}  // namespace cosmo
