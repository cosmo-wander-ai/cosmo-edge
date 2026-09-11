// ApiRouterRoutes.cc — Route registration for ApiRouter.
// Split from ApiRouter.cc to reduce file size (DEBT-007).

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <utility>

#include "api/ApiRouter.h"
#include "api/ApiRouterInternal.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/network/IAuthService.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo {
namespace {

    namespace fs = std::filesystem;

    class ScopedFd {
    public:
        explicit ScopedFd(int fd = -1) noexcept : fd_(fd) {}
        ~ScopedFd() {
            if (fd_ >= 0) {
                close(fd_);
            }
        }

        ScopedFd(const ScopedFd&)            = delete;
        ScopedFd& operator=(const ScopedFd&) = delete;

        [[nodiscard]] int Get() const noexcept {
            return fd_;
        }

    private:
        int fd_;
    };

    bool ResolveManagedTemporaryDownload(const std::string& candidate, std::string& resolved_path) {
        resolved_path.clear();
        std::error_code path_error;
        const auto root = fs::canonical(path::GetTemporaryDirPath(), path_error);
        if (path_error || root.empty()) {
            return false;
        }

        const fs::path requested(candidate);
        if (!requested.is_absolute() || !path::IsSafePathComponent(requested.filename().string()) ||
            requested.lexically_normal().parent_path() != root) {
            return false;
        }

        ScopedFd root_fd(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (root_fd.Get() < 0) {
            return false;
        }
        struct stat root_stat {};
        if (fstat(root_fd.Get(), &root_stat) != 0 || !S_ISDIR(root_stat.st_mode)) {
            return false;
        }

        // The data volume can be provisioned by the installation account while
        // the engine runs as another account (root in the packaged service).
        // Ownership of the final file, not the parent directory, is the trust
        // boundary. The HTTP streamer reopens the file with O_NOFOLLOW and
        // repeats the owner, link-count, and inode checks before sending it.
        const auto name = requested.filename().string();
        ScopedFd file_fd(openat(root_fd.Get(), name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (file_fd.Get() < 0) {
            return false;
        }
        struct stat opened_stat {};
        if (fstat(file_fd.Get(), &opened_stat) != 0 || !S_ISREG(opened_stat.st_mode) ||
            opened_stat.st_uid != geteuid() || opened_stat.st_nlink != 1 || opened_stat.st_size <= 0) {
            return false;
        }

        struct stat named_stat {};
        if (fstatat(root_fd.Get(), name.c_str(), &named_stat, AT_SYMLINK_NOFOLLOW) != 0 ||
            named_stat.st_dev != opened_stat.st_dev || named_stat.st_ino != opened_stat.st_ino) {
            return false;
        }
        resolved_path = (root / name).string();
        return true;
    }

}  // namespace

void ApiRouter::RegisterModelRoutes() {
    // ── Model Management ──────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Page);
    ROUTE_CONTEXT("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Upload);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, List);
    ROUTE_CONTEXT("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Add);
    url_map_[util::ToLower("/gtw/cwai/atomic/Model/UploadTemp")] = {
        kAuth,
        {},
        [this](const RequestDispatchContext& context, const std::string& jsonStr,
               std::error_condition& errc) {
            return detail::DispatchJsonWithContext<Model::MsgUploadTempSend, Model::MsgUploadTempRecv>(
                GetMessageFrom(), *model_handler_, context, jsonStr, errc);
        }};
    url_map_[util::ToLower("/gtw/cwai/atomic/Model/CancelUpload")] = {
        kAuth,
        {},
        [this](const RequestDispatchContext& context, const std::string& jsonStr,
               std::error_condition& errc) {
            return detail::DispatchJsonWithContext<Model::MsgCancelUploadSend, Model::MsgCancelUploadRecv>(
                GetMessageFrom(), *model_handler_, context, jsonStr, errc);
        }};
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, UploadCapabilities);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, GetConfig);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, SaveConfig);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, GetModelComponents);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Delete);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Update);
    ROUTE_CONTEXT("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, ImportModel);

    // Export model config (special handling: return zip file content for download)
    url_map_[util::ToLower("/gtw/cwai/atomic/model/exportConfig")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<Model::MsgExportConfigSend, Model::MsgExportConfigRecv>(
                GetMessageFrom(), *model_handler_, jsonStr, errc);
        }};
    file_download_routes_.insert(util::ToLower("/gtw/cwai/atomic/model/exportConfig"));
}

void ApiRouter::RegisterScheduleRoutes() {
    // ── Schedule ──────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/schedule/", kAuth, schedule_handler_, Schedule, Add);
    ROUTE("/gtw/cwai/schedule/", kAuth, schedule_handler_, Schedule, Update);
    ROUTE("/gtw/cwai/schedule/", kAuth, schedule_handler_, Schedule, Page);
    ROUTE("/gtw/cwai/schedule/", kAuth, schedule_handler_, Schedule, Delete);
    ROUTE("/gtw/cwai/schedule/", kAuth, schedule_handler_, Schedule, SelectScheduleInfo);
}

void ApiRouter::RegisterEventRoutes() {
    // ── Event ──────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Event/", kAuth, event_handler_, Event, Page);
    ROUTE("/gtw/cwai/Event/", kAuth, event_handler_, Event, ExportAlarm);
    ROUTE("/gtw/cwai/Event/", kAuth, event_handler_, Event, QueryPassengerFlowNumber);
}

void ApiRouter::RegisterCameraRoutes() {
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Gb28181Manage);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, OnvifInterfaces);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, OnvifDiscover);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, OnvifProbe);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, OnvifGet);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, OnvifSave);
    // ── Camera ──────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Add);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Update);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Page);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Delete);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, BatchDelete);
    ROUTE_CONTEXT("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, AddVideo);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, GetPicture);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, QueryUsbCameraList);
}

void ApiRouter::RegisterTaskRoutes() {
    // ── Task ──────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, ModifyParam);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, QueryParam);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, ModifyArea);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, QueryArea);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, ModifyStrategy);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, QueryStrategy);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, SwitchTask);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, BatchSwitchTask);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, QuerySwitch);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, SaveOrUpdate);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, SelectConfigByAlgorithmId);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, SelectAllAlgorithmInfo);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, ListChannel);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, ApplyParamsBatch);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, Delete);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, BatchDelete);
    ROUTE("/gtw/cwai/Task/", kAuth, video_task_handler_, VideoTask, RunningDetail);
}

void ApiRouter::RegisterSystemRoutes() {
    // ── System ──────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryDeviceInfo);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryHardwareResource);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryTime);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, NTPDate);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ModifyTime);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ResetPictureQuality);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, SetPictureQuality);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryPictureQuality);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ResetAlarmVideoDuration);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, SetAlarmVideoDuration);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryAlarmVideoDuration);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ResetDevRestartParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ModifyDevRestartParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryDevRestartParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ResetSystem);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ExportFile);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, CheckUpgradeSpace);
    ROUTE_CONTEXT("/gtw/cwai/System/", kAuth, system_handler_, System, Upgrade);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryModelAuthorization);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, DownloadModelAuthorizationRequest);
    file_download_routes_.insert(util::ToLower("/gtw/cwai/System/DownloadModelAuthorizationRequest"));
    ROUTE_CONTEXT("/gtw/cwai/System/", kAuth, system_handler_, System, InstallModelAuthorization);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QuerySystemLogo);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, SetSystemLogo);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryDeviceStatus);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ModifyDebugMode);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryDebugMode);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ModifyShiledActions);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryShiledActions);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ThreadDebugInfo);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryPopUpParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, SetPopUpParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryHttpInterfaceParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, SetHttpInterfaceParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryMqttAdapterParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, SetMqttAdapterParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryRunModeParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ModifyRunModeParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryIotNetworkParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, ModifyIotNetworkParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryDocumentUrl);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, QueryResourceLimitParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, SetResourceLimitParam);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, DebugQuit);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, DebugSystemMem);
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, Dict);
}

void ApiRouter::RegisterLibraryRoutes() {
    // ── Face Library ────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Library/", kAuth, lib_handler_, Lib, ModifyFaceLib);
    ROUTE_CONTEXT("/gtw/cwai/Library/", kAuth, lib_handler_, Lib, ModifyFacePicLib);
    ROUTE("/gtw/cwai/Library/", kAuth, lib_handler_, Lib, QueryFaceLibInfo);
    ROUTE("/gtw/cwai/Library/", kAuth, lib_handler_, Lib, DeleteFaceLib);
    ROUTE("/gtw/cwai/Library/", kAuth, lib_handler_, Lib, QueryFaces);
    ROUTE("/gtw/cwai/Library/", kAuth, lib_handler_, Lib, DeletePerson);

    // ── Body Library ────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, ModifyPersonLib);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, DeletePersonLib);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, QueryPersonLibInfo);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, QueryPersonPictures);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, AddLibPerson);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, BindTaskPersonLib);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, DeleteLibPerson);
    ROUTE_CONTEXT("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, DetectPerson);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, GetPersonPicture);

    // ── Things Library ────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, ModifyThingsLib);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, DeleteThingsLib);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, QueryThingsLibInfo);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, QueryThingsPictures);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, GetThingsPicture);
    ROUTE_CONTEXT("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, AddLibThings);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, BindTaskThingsLib);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, DeleteLibThings);
}

void ApiRouter::RegisterFileRoutes() {
    // ── File Import ──────────────────────────────────────────────────────
    ROUTE_CONTEXT("/gtw/cwai/File/", kAuth, import_file_handler_, service, ImportFile);
    ROUTE("/gtw/cwai/File/", kAuth, import_file_handler_, service, QueryImportStatus);
}

void ApiRouter::RegisterAudioRoutes() {
    // ── Audio ──────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Audio/", kAuth, audio_handler_, Audio, QueryAudioFile);
    ROUTE("/gtw/cwai/Audio/", kAuth, audio_handler_, Audio, DeleteAudioFile);
    ROUTE("/gtw/cwai/Audio/", kAuth, audio_handler_, Audio, ModifyAudioDevice);
    ROUTE("/gtw/cwai/Audio/", kAuth, audio_handler_, Audio, QueryAudioDevice);
    ROUTE("/gtw/cwai/Audio/", kAuth, audio_handler_, Audio, DeleteAudioDevice);
    ROUTE("/gtw/cwai/Audio/", kAuth, audio_handler_, Audio, TestAudioDevice);
}

void ApiRouter::RegisterLinkageRoutes() {
    // ── Linkage ──────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/AlarmStrage/", kAuth, linkage_handler_, Linkage, Storages);
    ROUTE("/gtw/cwai/AlarmStrage/", kAuth, linkage_handler_, Linkage, Add);
    ROUTE("/gtw/cwai/AlarmStrage/", kAuth, linkage_handler_, Linkage, Delete);
    ROUTE("/gtw/cwai/AlarmStrage/", kAuth, linkage_handler_, Linkage, Update);
    ROUTE("/gtw/cwai/AlarmStrage/", kAuth, linkage_handler_, Linkage, Page);
    ROUTE("/gtw/cwai/AlarmStrage/", kAuth, linkage_handler_, Linkage, Switch);
}

void ApiRouter::RegisterLiveStreamRoutes() {
    // ── Live Stream ──────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/LiveStream/", kAuth, live_stream_handler_, LiveStream, RequestLiveStream);
    ROUTE("/gtw/cwai/LiveStream/", kAuth, live_stream_handler_, LiveStream, StreamKeepAlive);
    ROUTE("/gtw/cwai/LiveStream/", kAuth, live_stream_handler_, LiveStream, StreamStop);
}

void ApiRouter::RegisterOnboardingRoutes() {
    // ── Onboarding Guide ──────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Onboarding/", kNoAuth, onboarding_handler_, Onboarding, Status);
    ROUTE("/gtw/cwai/Onboarding/", kNoAuth, onboarding_handler_, Onboarding, Complete);
    ROUTE("/gtw/cwai/Onboarding/", kAuth, onboarding_handler_, Onboarding, Reset);
}

bool ApiRouter::ResolveFileDownload(const std::string& json_response, RequestDispatchResponse& response) {
    auto doc = nlohmann::json::parse(json_response, nullptr, false);
    if (doc.is_object() && doc.contains("filePath") && doc["filePath"].is_string()) {
        const auto file_path = doc["filePath"].get<std::string>();
        std::string resolved_path;
        if (file_path.empty() || !ResolveManagedTemporaryDownload(file_path, resolved_path)) {
            LOG_WARN("{}", "Reject unmanaged download response path");
            return false;
        }

        std::string file_name = fs::path(resolved_path).filename().string();
        if (doc.contains("fileName") && doc["fileName"].is_string()) {
            const auto candidate_name = doc["fileName"].get<std::string>();
            if (path::IsSafePathComponent(candidate_name)) {
                file_name = candidate_name;
            }
        }

        response.file_path              = std::move(resolved_path);
        response.file_name              = std::move(file_name);
        response.delete_file_after_send = true;
        response.body.clear();
        return true;
    }
    return false;
}

bool ApiRouter::SupportsRoute(const std::string& interface) {
    auto it = url_map_.find(util::ToLower(interface));
    if (it == url_map_.end()) {
        return false;
    }
    return true;
}

RequestAdmission ApiRouter::InspectRequest(RequestDispatchContext& context, bool require_known_route) {
    // Always discard caller-provided identity before evaluating transport
    // credentials. Resource ownership and admission quotas may only use the
    // server-derived principal populated below.
    context.principal.clear();
    const auto expected_transport =
        MessageFromType::MessageFromMqtt == from_ ? RequestTransport::kMqtt : RequestTransport::kHttp;
    if (context.transport != expected_transport) {
        return RequestAdmission::kUnauthorized;
    }

    InterfaceMsgAuthType auth_type = InterfaceMsgAuthType::Mtk;
    if (!require_known_route) {
        if (RequestTransport::kHttp != context.transport) {
            return RequestAdmission::kUnauthorized;
        }
    } else {
        auto it = url_map_.find(util::ToLower(context.uri));
        if (it == url_map_.end()) {
            return RequestAdmission::kRouteNotFound;
        }
        auth_type = it->second.authType;
    }

    if (!CredentialValid(context, auth_type)) {
        return RequestAdmission::kUnauthorized;
    }
    return RequestAdmission::kAllowed;
}

bool ApiRouter::DispatchRequest(const RequestDispatchContext& context, const std::string& message,
                                std::string& response) {
    std::error_condition errc = util::ErrorEnum::Success;

    RequestDispatchContext authorized_context = context;
    const auto admission                      = InspectRequest(authorized_context, true);
    if (RequestAdmission::kUnauthorized == admission) {
        errc     = util::ErrorEnum::AuthFailed;
        response = detail::ErroResult("Auth Failed", errc);
        return false;
    }
    if (RequestAdmission::kRouteNotFound == admission) {
        errc     = util::ErrorEnum::InterfaceNotSupport;
        response = detail::ErroResult("Interface Not Support", errc);
        return false;
    }

    auto it = url_map_.find(util::ToLower(context.uri));
    if (it->second.context_func) {
        response = it->second.context_func(authorized_context, message, errc);
    } else {
        response = it->second.func(message, errc);
    }
    return true;
}

bool ApiRouter::DispatchRequestResponse(const RequestDispatchContext& context, const std::string& message,
                                        RequestDispatchResponse& response) {
    if (!DispatchRequest(context, message, response.body)) {
        return false;
    }
    if (context.transport != RequestTransport::kHttp ||
        file_download_routes_.count(util::ToLower(context.uri)) == 0) {
        return true;
    }
    const auto result = nlohmann::json::parse(response.body, nullptr, false);
    if (result.is_object() && result.value("resCode", kServerRspFailed) != kServerRspSuccess) {
        // Preserve business-policy failures such as DefaultCantBeExport. Only a
        // successful export is required to resolve to a managed attachment.
        return true;
    }
    if (!ResolveFileDownload(response.body, response)) {
        // A successful handler response may contain an internal temporary path.
        // Never return that path to the client when it cannot be converted into
        // a managed download.
        std::error_condition errc = util::ErrorEnum::FileOpenFailed;
        response.body             = detail::ErroResult("Export file is unavailable for download", errc);
    }
    return true;
}

bool ApiRouter::DispatchRequest(const std::string& interface, const std::string& credential,
                                const std::string& message, std::string& response) {
    RequestDispatchContext context;
    context.uri        = interface;
    context.credential = credential;
    context.transport =
        MessageFromType::MessageFromMqtt == from_ ? RequestTransport::kMqtt : RequestTransport::kHttp;
    return DispatchRequest(context, message, response);
}

bool ApiRouter::CredentialValid(RequestDispatchContext& context, InterfaceMsgAuthType interface_auth_type) {
    if (InterfaceMsgAuthType::None == interface_auth_type) {
        return true;
    }

    if (InterfaceMsgAuthType::Mtk != interface_auth_type) {
        return false;
    }

    if (RequestTransport::kMqtt == context.transport) {
        return true;
    }

    // If IAuthService is not registered, we cannot authenticate the request.
    if (!service::ServiceRegistry::Instance().Has<service::IAuthService>()) {
        return false;
    }
    return service::ServiceRegistry::Instance().Get<service::IAuthService>().ResolvePrincipal(
               context.credential, context.principal) &&
           !context.principal.empty();
}

MessageHandler& ApiRouter::Handler() {
    return *handler_;
}

}  // namespace cosmo
