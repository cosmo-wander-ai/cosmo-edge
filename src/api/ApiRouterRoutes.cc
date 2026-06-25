// ApiRouterRoutes.cc — Route registration for ApiRouter.
// Split from ApiRouter.cc to reduce file size (DEBT-007).

#include <filesystem>

#include "api/ApiRouter.h"
#include "api/ApiRouterInternal.h"
#include "nlohmann/json.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/network/IAuthService.h"
#include "util/FileUtil.h"
#include "util/PathUtil.h"

namespace cosmo {

void ApiRouter::RegisterModelRoutes() {
    // ── Model Management ──────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Page);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Upload);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, List);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Add);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, UploadTemp);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, GetConfig);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, SaveConfig);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, GetModelComponents);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Delete);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, Update);
    ROUTE("/gtw/cwai/atomic/Model/", kAuth, model_handler_, Model, ImportModel);

    // Export model config (special handling: return zip file content for download)
    url_map_[util::ToLower("/gtw/cwai/atomic/model/exportConfig")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            std::string jsonResponse =
                detail::DispatchJson<Model::MsgExportConfigSend, Model::MsgExportConfigRecv>(
                    GetMessageFrom(), *model_handler_, jsonStr, errc);
            return DispatchFileDownload(jsonResponse);
        }};
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
    // ── Camera ──────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Add);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Update);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Page);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, Delete);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, BatchDelete);
    ROUTE("/gtw/cwai/Camera/", kAuth, camera_handler_, camera, AddVideo);
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
    ROUTE("/gtw/cwai/System/", kAuth, system_handler_, System, Upgrade);
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
    ROUTE("/gtw/cwai/Library/", kAuth, lib_handler_, Lib, ModifyFacePicLib);
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
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, DetectPerson);
    ROUTE("/gtw/cwai/BodyLibrary/", kAuth, body_lib_handler_, BodyLib, GetPersonPicture);

    // ── Things Library ────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, ModifyThingsLib);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, DeleteThingsLib);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, QueryThingsLibInfo);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, QueryThingsPictures);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, GetThingsPicture);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, AddLibThings);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, BindTaskThingsLib);
    ROUTE("/gtw/cwai/ThingsLibrary/", kAuth, things_lib_handler_, ThingsLib, DeleteLibThings);
}

void ApiRouter::RegisterFileRoutes() {
    // ── File Import ──────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/File/", kAuth, import_file_handler_, service, ImportFile);
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

std::string ApiRouter::DispatchFileDownload(const std::string& jsonResponse) {
    auto doc = nlohmann::json::parse(jsonResponse, nullptr, false);
    if (doc.is_object() && doc.contains("filePath") && doc["filePath"].is_string()) {
        std::string filePath = doc["filePath"].get<std::string>();
        if (!filePath.empty()) {
            std::vector<uint8_t> fileContentBin = util::ReadFileBin(filePath);
            if (!fileContentBin.empty()) {
                std::string fileContent(fileContentBin.begin(), fileContentBin.end());
                try {
                    std::filesystem::remove(filePath);
                } catch (const std::exception& e) {
                    LOG_WARN("Failed to remove temp file {}: {}", filePath, e.what());
                }
                return fileContent;
            }
        }
    }
    return jsonResponse;
}

bool ApiRouter::SupportsRoute(const std::string& interface) {
    auto it = url_map_.find(util::ToLower(interface));
    if (it == url_map_.end()) {
        return false;
    }
    return true;
}

bool ApiRouter::DispatchRequest(const std::string& interface, const std::string& message,
                                std::string& response) {
    auto it = url_map_.find(util::ToLower(interface));
    if (it == url_map_.end()) {
        return false;
    }
    std::error_condition errc = util::ErrorEnum::Success;
    response                  = it->second.func(message, errc);
    return true;
}

bool ApiRouter::DispatchRequest(const std::string& interface, const std::string& mtk,
                                const std::string& message, std::string& response) {
    std::error_condition errc = util::ErrorEnum::Success;

    auto it = url_map_.find(util::ToLower(interface));
    if (it == url_map_.end()) {
        errc     = util::ErrorEnum::InterfaceNotSupport;
        response = detail::ErroResult("Interface Not Support", errc);
        return false;
    }

    InterfaceMsgAuthType interfaceAuthType = it->second.authType;
    if (!MtkValid(mtk, interfaceAuthType)) {
        errc     = util::ErrorEnum::AuthFailed;
        response = detail::ErroResult("Auth Failed", errc);
        return false;
    }

    response = it->second.func(message, errc);
    return true;
}

bool ApiRouter::MtkValid(const std::string& mtk, InterfaceMsgAuthType interfaceAuthType) {
    if (MessageFromType::MessageFromHttp != from_) {
        return true;
    }
    if (InterfaceMsgAuthType::Mtk != interfaceAuthType) {
        return true;
    }
    // If IAuthService is not registered, we cannot authenticate the request.
    if (!service::ServiceRegistry::Instance().Has<service::IAuthService>()) {
        return false;
    }
    return service::ServiceRegistry::Instance().Get<service::IAuthService>().IsValidToken(mtk);
}

MessageHandler& ApiRouter::Handler() {
    return *handler_;
}

}  // namespace cosmo
