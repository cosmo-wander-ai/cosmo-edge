// MessageModelHandler — thin proxy layer for model management API.
// All business logic is delegated to IModelService.

#include "api/MessageModelHandler.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

#include "api/HttpUploadClaim.h"
#include "api/StagedImageInput.h"
#include "media/EncodedImageInfo.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelService.h"
#include "service/path/IUploadStagingService.h"
#include "util/ErrorCode.h"
#include "util/VideoInfo.h"

namespace cosmo {
namespace {

    template <typename T>
    bool ParseUnsigned(const std::string& value, T& result) {
        if (value.empty() || value.front() == '-') {
            return false;
        }
        T parsed{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} || end != value.data() + value.size()) {
            return false;
        }
        result = parsed;
        return true;
    }

    bool HasAnySuffix(const std::string& value, std::initializer_list<std::string_view> suffixes) {
        return std::any_of(suffixes.begin(), suffixes.end(), [&](std::string_view suffix) {
            return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(),
                                                                  suffix.data(), suffix.size()) == 0;
        });
    }

    bool InferLegacyUploadPurpose(const std::string& file_name, service::UploadPurpose& purpose) {
        std::string lower_name = file_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

        const bool is_video = HasAnySuffix(lower_name, {".mp4", ".mkv", ".avi", ".dav"});
        const bool is_model_component =
            HasAnySuffix(lower_name, {".bmodel", ".onnx", ".rknn", ".txt", ".json"});
        const bool is_model_archive = HasAnySuffix(lower_name, {".zip", ".tgz", ".tar.gz"});
        const int match_count       = static_cast<int>(is_video) + static_cast<int>(is_model_component) +
                                static_cast<int>(is_model_archive);
        if (match_count != 1) {
            return false;
        }
        if (is_video) {
            purpose = service::UploadPurpose::kVideo;
        } else if (is_model_component) {
            purpose = service::UploadPurpose::kModelComponent;
        } else {
            purpose = service::UploadPurpose::kModelArchive;
        }
        return true;
    }

    MsgResBase MakeUploadAdmissionError(const service::UploadSessionInfo& info,
                                        service::UploadPurpose purpose) {
        MsgResBase message;
        message.details.resource = "upload-staging";
        message.details.purpose  = std::string(service::UploadPurposeName(purpose));
        message.retryable        = true;

        using Failure = service::UploadAdmissionFailure;
        switch (info.admission.failure) {
            case Failure::kFilePolicy:
                message.msgCode             = "UPLOAD_FILE_POLICY_LIMIT";
                message.messageKey          = "api.error.uploadFilePolicyLimit";
                message.msgText             = "File exceeds the deployment upload policy";
                message.details.actualBytes = info.admission.actual;
                message.details.limitBytes  = info.admission.limit;
                message.retryable           = false;
                message.recommendedAction   = "CHANGE_DEPLOYMENT_POLICY";
                break;
            case Failure::kChunkPolicy:
                message.msgCode             = "UPLOAD_CHUNK_POLICY_LIMIT";
                message.messageKey          = "api.error.uploadChunkPolicyLimit";
                message.msgText             = "Upload uses more chunks than the deployment policy";
                message.details.actualCount = info.admission.actual;
                message.details.limitCount  = info.admission.limit;
                message.retryable           = false;
                message.recommendedAction   = "USE_LARGER_CHUNKS_OR_CHANGE_POLICY";
                break;
            case Failure::kMetadataBudget:
                message.msgCode             = "UPLOAD_METADATA_BUDGET";
                message.messageKey          = "api.error.uploadMetadataBudget";
                message.msgText             = "Upload chunk metadata would exceed the safe memory budget";
                message.details.actualBytes = info.admission.actual;
                message.details.limitBytes  = info.admission.limit;
                message.retryable           = false;
                message.recommendedAction   = "USE_LARGER_CHUNKS";
                break;
            case Failure::kPrincipalSessions:
            case Failure::kGlobalSessions:
                message.msgCode             = "TRANSFER_BUSY";
                message.messageKey          = "api.error.transferBusy";
                message.msgText             = "The configured upload session capacity is busy";
                message.details.actualCount = info.admission.actual;
                message.details.limitCount  = info.admission.limit;
                message.recommendedAction   = "RETRY_LATER";
                message.retryAfterSeconds   = 5;
                break;
            case Failure::kStorageReserve:
                message.msgCode                = "STORAGE_RESERVE_REACHED";
                message.messageKey             = "api.error.storageReserveReached";
                message.msgText                = "Insufficient safe disk space for this upload";
                message.details.requiredBytes  = info.admission.required_bytes;
                message.details.availableBytes = info.admission.available_bytes;
                message.details.reserveBytes   = info.admission.reserve_bytes;
                message.details.limitBytes     = info.admission.limit;
                message.recommendedAction      = "FREE_DISK_SPACE";
                break;
            case Failure::kNone:
                message.msgCode           = "UPLOAD_REJECTED";
                message.messageKey        = "api.error.uploadRejected";
                message.msgText           = "Upload was rejected";
                message.recommendedAction = "CHECK_UPLOAD_PARAMETERS";
                message.retryable         = false;
                break;
        }
        return message;
    }

}  // namespace

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

Model::MsgUploadSend MessageModelHandler::Handle(Model::MsgUploadRecv&& data,
                                                 const RequestDispatchContext& context,
                                                 std::error_condition& errc) const {
    Model::MsgUploadSend result{};
    if (context.transport != RequestTransport::kHttp || context.principal.empty()) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    service::StagedFileLease lease;
    if (!data.uploadId.empty()) {
        errc = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>().Consume(
            context.principal, data.uploadId, service::UploadPurpose::kModelArchive, lease);
    } else {
        errc = detail::ClaimHttpUpload(context, data.filePath, service::UploadPurpose::kModelArchive, lease);
    }
    if (errc) {
        return result;
    }
    if (!lease.Revalidate()) {
        errc = util::ErrorEnum::FileAnalysisFailed;
        return result;
    }
    data.filePath = lease.Path();
    return Handle(std::move(data), errc);
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
    errc =
        model_service_.AddAtomicModel(data.modelCode, data.modelName, data.modelType, data.description,
                                      data.bmodelFiles, data.vocabFilePath, data.tokenizerFilePath,
                                      data.characterTableFilePath, data.normalizationMode, data.colorChannel);
    return retData;
}

Model::MsgAddSend MessageModelHandler::Handle(Model::MsgAddRecv&& data, const RequestDispatchContext& context,
                                              std::error_condition& errc) const {
    Model::MsgAddSend result{};
    if (context.transport != RequestTransport::kHttp || context.principal.empty()) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    if (data.bmodelFiles.empty()) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    enum class ReferenceKind { kUnset, kUploadId, kLegacyPath };
    ReferenceKind reference_kind = ReferenceKind::kUnset;
    std::vector<std::string> references;
    std::vector<std::string*> claimed_paths;
    references.reserve(data.bmodelFiles.size() + 3);
    claimed_paths.reserve(data.bmodelFiles.size() + 3);

    auto add_reference = [&](const std::string& upload_id, std::string& file_path, bool required) {
        if (!upload_id.empty() && !file_path.empty()) {
            return false;
        }
        if (upload_id.empty() && file_path.empty()) {
            return !required;
        }
        const auto kind = upload_id.empty() ? ReferenceKind::kLegacyPath : ReferenceKind::kUploadId;
        if (reference_kind != ReferenceKind::kUnset && reference_kind != kind) {
            return false;
        }
        reference_kind = kind;
        references.push_back(upload_id.empty() ? file_path : upload_id);
        claimed_paths.push_back(&file_path);
        return true;
    };

    for (auto& file : data.bmodelFiles) {
        if (!add_reference(file.uploadId, file.filePath, true)) {
            errc = util::ErrorEnum::InvalidParam;
            return result;
        }
    }
    if (!add_reference(data.vocabUploadId, data.vocabFilePath, false) ||
        !add_reference(data.tokenizerUploadId, data.tokenizerFilePath, false) ||
        !add_reference(data.characterTableUploadId, data.characterTableFilePath, false)) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    std::vector<service::StagedFileLease> leases;
    auto& staging = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>();
    if (reference_kind == ReferenceKind::kUploadId) {
        errc = staging.ConsumeMany(context.principal, references, service::UploadPurpose::kModelComponent,
                                   leases);
    } else {
        errc = staging.ConsumeLegacyPaths(context.principal, references,
                                          service::UploadPurpose::kModelComponent, leases);
    }
    if (errc) {
        return result;
    }
    if (leases.size() != claimed_paths.size()) {
        errc = util::ErrorEnum::FileOpenFailed;
        return result;
    }
    if (std::any_of(leases.begin(), leases.end(),
                    [](const service::StagedFileLease& lease) { return !lease.Revalidate(); })) {
        errc = util::ErrorEnum::FileAnalysisFailed;
        return result;
    }
    for (std::size_t i = 0; i < leases.size(); ++i) {
        *claimed_paths[i] = leases[i].Path();
    }
    return Handle(std::move(data), errc);
}

// Upload temp file
Model::MsgUploadTempSend MessageModelHandler::Handle(Model::MsgUploadTempRecv&& data,
                                                     const RequestDispatchContext& context,
                                                     std::error_condition& errc) const {
    Model::MsgUploadTempSend result{};
    if (context.transport != RequestTransport::kHttp || context.principal.empty() || data.filePath.empty() ||
        data.fileName.empty() || !detail::IsCurrentMultipartFile(context, data.filePath) ||
        data.fileName != context.multipart_file_name) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    std::uint32_t chunk_index{0};
    std::uint32_t total_chunks{1};
    std::uint64_t total_size{0};
    std::uint64_t chunk_size{0};
    if ((!data.chunkIndex.empty() && !ParseUnsigned(data.chunkIndex, chunk_index)) ||
        (!data.totalChunks.empty() && !ParseUnsigned(data.totalChunks, total_chunks)) ||
        !ParseUnsigned(data.totalSize.empty() ? data.contentLength : data.totalSize, total_size) ||
        !ParseUnsigned(data.contentLength, chunk_size) || chunk_size != context.multipart_file_size) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }
    if (!data.chunkSize.empty()) {
        std::uint64_t declared_chunk_size{0};
        if (!ParseUnsigned(data.chunkSize, declared_chunk_size) || declared_chunk_size != chunk_size) {
            errc = util::ErrorEnum::InvalidParam;
            return result;
        }
    }
    if (total_size < chunk_size) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    service::UploadPurpose purpose;
    if ((!data.purpose.empty() && !service::ParseUploadPurpose(data.purpose, purpose)) ||
        (data.purpose.empty() && !InferLegacyUploadPurpose(data.fileName, purpose))) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    auto& staging = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>();
    service::UploadSessionInfo info;
    std::string upload_id;
    bool created_session = false;
    if (chunk_index == 0) {
        service::UploadBeginRequest request;
        request.principal     = context.principal;
        request.purpose       = purpose;
        request.original_name = data.fileName;
        request.total_size    = total_size;
        request.total_chunks  = total_chunks;
        request.sha256        = data.sha256;
        // New clients use clientRequestId. R1 clients placed their own stable
        // alias in uploadId from chunk zero onward. Register exactly one alias.
        request.client_request_id = data.clientRequestId.empty() ? data.uploadId : data.clientRequestId;
        errc                      = staging.Begin(request, info);
        if (errc) {
            if (info.admission.failure != service::UploadAdmissionFailure::kNone) {
                result.resMsg.push_back(MakeUploadAdmissionError(info, purpose));
            }
            return result;
        }
        upload_id       = info.upload_id;
        created_session = info.newly_created;
    } else {
        if (data.uploadId.empty()) {
            errc = util::ErrorEnum::InvalidParam;
            return result;
        }
        upload_id = data.uploadId;
    }

    errc = staging.AppendChunk(context.principal, upload_id, chunk_index, data.filePath, info);
    if (errc) {
        if (created_session) {
            staging.Cancel(context.principal, upload_id);
        }
        return result;
    }
    // Append resolves any principal-scoped R1 alias to the canonical server ID.
    upload_id = info.upload_id;

    if (info.next_chunk_index == info.total_chunks) {
        errc = staging.Complete(context.principal, upload_id, info);
        if (errc) {
            staging.Cancel(context.principal, upload_id);
            return result;
        }
        errc = staging.GetLegacyPath(context.principal, upload_id, purpose, result.resData.filePath);
        if (errc) {
            staging.Cancel(context.principal, upload_id);
            return result;
        }
    }

    result.resData.uploadId       = upload_id;
    result.resData.nextChunkIndex = std::to_string(info.next_chunk_index);
    result.resData.complete       = info.complete;
    errc                          = util::ErrorEnum::Success;
    return result;
}

Model::MsgCancelUploadSend MessageModelHandler::Handle(Model::MsgCancelUploadRecv&& data,
                                                       const RequestDispatchContext& context,
                                                       std::error_condition& errc) const {
    Model::MsgCancelUploadSend result{};
    if (context.transport != RequestTransport::kHttp || context.principal.empty() || data.uploadId.empty()) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }
    errc = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>().Cancel(
        context.principal, data.uploadId);
    return result;
}

Model::MsgUploadCapabilitiesSend MessageModelHandler::Handle(Model::MsgUploadCapabilitiesRecv&& /*data*/,
                                                             std::error_condition& errc) const {
    Model::MsgUploadCapabilitiesSend result;
    const auto capabilities =
        service::ServiceRegistry::Instance().Get<service::IUploadStagingService>().GetCapabilities();
    result.resData.maxTotalSize                = std::to_string(capabilities.max_total_size);
    result.resData.maxChunkSize                = std::to_string(capabilities.max_chunk_size);
    result.resData.maxChunks                   = std::to_string(capabilities.max_chunks);
    result.resData.idleTimeoutMs               = std::to_string(capabilities.idle_timeout_ms);
    result.resData.absoluteTimeoutMs           = std::to_string(capabilities.absolute_timeout_ms);
    result.resData.availableBytes              = std::to_string(capabilities.available_bytes);
    result.resData.reserveBytes                = std::to_string(capabilities.reserve_bytes);
    result.resData.availableForNewUploadsBytes = std::to_string(capabilities.available_for_new_uploads_bytes);
    result.resData.reservedBySessionsBytes     = std::to_string(capabilities.reserved_by_sessions_bytes);
    result.resData.activeSessions              = std::to_string(capabilities.active_sessions);
    result.resData.maxEncodedImageBytes        = std::to_string(detail::MaxEncodedImageInputBytes());
    result.resData.maxImagePixels              = std::to_string(media::MaxDecodedImagePixels());
    result.resData.resumable                   = capabilities.resumable;
    result.resData.persistentAcrossRestart     = capabilities.persistent_across_restart;
    errc                                       = util::ErrorEnum::Success;
    return result;
}

// Get model config
Model::MsgGetConfigSend MessageModelHandler::Handle(Model::MsgGetConfigRecv&& data,
                                                    std::error_condition& errc) const {
    Model::MsgGetConfigSend retData{};
    errc = model_service_.GetModelConfig(data.modelCode, retData.resData.configJson,
                                         retData.resData.isExportable, retData.resData.defaultConfigJson);
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

Model::MsgImportModelSend MessageModelHandler::Handle(Model::MsgImportModelRecv&& data,
                                                      const RequestDispatchContext& context,
                                                      std::error_condition& errc) const {
    Model::MsgImportModelSend result{};
    if (context.transport != RequestTransport::kHttp || context.principal.empty()) {
        errc = util::ErrorEnum::InvalidParam;
        return result;
    }

    service::StagedFileLease lease;
    if (!data.uploadId.empty()) {
        errc = service::ServiceRegistry::Instance().Get<service::IUploadStagingService>().Consume(
            context.principal, data.uploadId, service::UploadPurpose::kModelArchive, lease);
    } else {
        errc = detail::ClaimHttpUpload(context, data.filePath, service::UploadPurpose::kModelArchive, lease);
    }
    if (errc) {
        return result;
    }
    if (!lease.Revalidate()) {
        errc = util::ErrorEnum::FileAnalysisFailed;
        return result;
    }
    data.filePath = lease.Path();
    return Handle(std::move(data), errc);
}
}  // namespace cosmo
