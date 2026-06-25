// ApiRouter — API route dispatcher.

#include "api/ApiRouter.h"

#include <filesystem>

#include "api/ApiRouterInternal.h"
#include "service/algorithm/IAlgorithmCrud.h"
#include "service/algorithm/IAlgorithmLayout.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/camera/ICameraChannelQuery.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmRecordService.h"
#include "service/face/IArticlesReidDaoService.h"
#include "service/face/IBodyLibService.h"
#include "service/face/IFaceFeature.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IFaceLibService.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRecogDaoService.h"
#include "service/face/IPersonRepo.h"
#include "service/infra/ILinkageService.h"
#include "service/media/IAudioService.h"
#include "service/media/ILiveStreamService.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/onboarding/IOnboardingService.h"
#include "service/model/IModelService.h"
#include "service/network/IAuthService.h"
#include "service/network/INetworkService.h"
#include "service/system/IConfigNetworkService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IConfigWriteService.h"
#include "service/system/IDeviceInfoService.h"
#include "service/system/ISystemOperationService.h"
#include "service/system/ITimeService.h"
#include "service/task/IScheduleService.h"
#include "service/task/ITaskQuery.h"
#include "util/FileUtil.h"
#include "util/PathUtil.h"

namespace cosmo {

ApiRouter::ApiRouter(MessageFromType from)
    : handler_(std::make_unique<MessageHandler>()),
      auth_handler_(std::make_unique<MessageAuthHandler>(
          service::ServiceRegistry::Instance().Get<service::IAuthService>())),
      network_handler_(std::make_unique<MessageNetworkHandler>(
          service::ServiceRegistry::Instance().Get<service::INetworkService>())),
      algorithm_handler_(std::make_unique<MessageAlgorithmHandler>(
          service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>(),
          service::ServiceRegistry::Instance().Get<service::IAlgorithmCrud>(),
          service::ServiceRegistry::Instance().Get<service::IAlgorithmLayout>(),
          service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>(),
          service::ServiceRegistry::Instance().Get<service::ITaskQuery>())),
      model_handler_(std::make_unique<MessageModelHandler>(
          service::ServiceRegistry::Instance().Get<service::IModelService>())),
      schedule_handler_(std::make_unique<MessageScheduleHandler>(
          service::ServiceRegistry::Instance().Get<service::IScheduleService>(),
          service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>())),
      event_handler_(std::make_unique<MessageEventHandler>(
          service::ServiceRegistry::Instance().Get<service::IAlarmRecordService>(),
          service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>(),
          service::ServiceRegistry::Instance().Get<service::INetworkService>())),
      camera_handler_(std::make_unique<MessageCameraHandler>(
          service::ServiceRegistry::Instance().Get<service::ICameraDeviceCrud>(),
          service::ServiceRegistry::Instance().Get<service::ICameraChannelQuery>(),
          service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>(),
          service::ServiceRegistry::Instance().Get<service::ITaskQuery>())),
      video_task_handler_(std::make_unique<MessageVideoTaskHandler>(
          service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>(),
          service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>(),
          service::ServiceRegistry::Instance().Get<service::ICameraDeviceCrud>(),
          service::ServiceRegistry::Instance().Get<service::ITaskQuery>())),
      system_handler_(std::make_unique<MessageSystemHandler>(
          service::ServiceRegistry::Instance().Get<service::IConfigReadService>(),
          service::ServiceRegistry::Instance().Get<service::IConfigWriteService>(),
          service::ServiceRegistry::Instance().Get<service::IConfigNetworkService>(),
          service::ServiceRegistry::Instance().Get<service::IDeviceInfoService>(),
          service::ServiceRegistry::Instance().Get<service::ISystemOperationService>(),
          service::ServiceRegistry::Instance().Get<service::ITimeService>())),
      live_stream_handler_(std::make_unique<MessageLiveStreamHandler>(
          service::ServiceRegistry::Instance().Get<service::ILiveStreamService>())),
      lib_handler_(std::make_unique<MessageFaceLibHandler>(
          service::ServiceRegistry::Instance().Get<service::IFaceLibRepo>(),
          service::ServiceRegistry::Instance().Get<service::IPersonRepo>(),
          service::ServiceRegistry::Instance().Get<service::IFaceFeature>(),
          service::ServiceRegistry::Instance().Get<service::IPersonDaoService>(),
          service::ServiceRegistry::Instance().Get<service::IFaceLibService>(),
          service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>())),
      import_file_handler_(std::make_unique<MessageImportFileHandler>()),
      body_lib_handler_(std::make_unique<MessageBodyLibHandler>(
          service::ServiceRegistry::Instance().Get<service::IPersonRecogDaoService>(),
          service::ServiceRegistry::Instance().Get<service::IBodyLibService>(),
          service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>(),
          service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>())),
      things_lib_handler_(std::make_unique<MessageThingsLibHandler>(
          service::ServiceRegistry::Instance().Get<service::IArticlesReidDaoService>(),
          service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>(),
          service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>())),
      audio_handler_(std::make_unique<MessageAudioHandler>(
          service::ServiceRegistry::Instance().Get<service::IAudioService>(),
          service::ServiceRegistry::Instance().Get<service::ILinkageService>())),
      linkage_handler_(std::make_unique<MessageLinkageHandler>(
          service::ServiceRegistry::Instance().Get<service::ILinkageService>())),
      onboarding_handler_(std::make_unique<MessageOnboardingHandler>(
          service::ServiceRegistry::Instance().Get<service::IOnboardingService>())),
      from_(from) {
    RegisterCoreRoutes();
    RegisterNetworkRoutes();
    RegisterAlgorithmRoutes();
    RegisterModelRoutes();
    RegisterScheduleRoutes();
    RegisterEventRoutes();
    RegisterCameraRoutes();
    RegisterTaskRoutes();
    RegisterSystemRoutes();
    RegisterLibraryRoutes();
    RegisterFileRoutes();
    RegisterAudioRoutes();
    RegisterLinkageRoutes();
    RegisterLiveStreamRoutes();
    RegisterOnboardingRoutes();
}

void ApiRouter::RegisterCoreRoutes() {
    // ── Core API (No Auth, /v1/cwai/aihost/) ──────────────────────────
    ROUTE_CORE("/v1/cwai/aihost/", InterfaceTest);
    ROUTE_CORE("/v1/cwai/aihost/", TaskCreate);
    ROUTE_CORE("/v1/cwai/aihost/", TaskCancle);
    ROUTE_CORE("/v1/cwai/aihost/", PTaskCreate);
    ROUTE_CORE("/v1/cwai/aihost/", PTaskCancle);
    ROUTE_CORE("/v1/cwai/aihost/", PTaskDetectPic);
    ROUTE_CORE("/v1/cwai/aihost/", OperateNode);
    ROUTE_CORE("/v1/cwai/aihost/", Info);
    ROUTE_CORE("/v1/cwai/aihost/", Probe);
    ROUTE_CORE("/v1/cwai/aihost/", ViewRoutes);
    ROUTE_CORE("/v1/cwai/aihost/", GraphicsMemory);
    ROUTE_CORE("/v1/cwai/aihost/", OverviewStructrueRecord);
    ROUTE_CORE("/v1/cwai/aihost/", LoadLocalAlgorithmAction);
    ROUTE_CORE("/v1/cwai/aihost/", LogicTest);
    ROUTE_CORE("/v1/cwai/aihost/", QueryTaskOverviewFile);
    ROUTE_CORE("/v1/cwai/aihost/", QueryTaskStatus);
    ROUTE_CORE("/v1/cwai/aihost/", QueryTaskInfo);
    ROUTE_CORE("/v1/cwai/aihost/", QueryDeviceMemStatus);
    ROUTE_CORE("/v1/cwai/aihost/", QueryLogs);

    // ── Core API compatible route (unified /gtw/cwai/ prefix for frontend) ────────────
    url_map_[util::ToLower("/gtw/cwai/aihost/PTaskCreate")] = {
        kNoAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<MsgPTaskCreateSend, MsgPTaskCreateRecv>(GetMessageFrom(), *handler_,
                                                                                jsonStr, errc);
        }};
    url_map_[util::ToLower("/gtw/cwai/aihost/PTaskCancle")] = {
        kNoAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<MsgPTaskCancleSend, MsgPTaskCancleRecv>(GetMessageFrom(), *handler_,
                                                                                jsonStr, errc);
        }};
    url_map_[util::ToLower("/gtw/cwai/aihost/PTaskDetectPic")] = {
        kNoAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<MsgPTaskDetectPicSend, MsgPTaskDetectPicRecv>(
                GetMessageFrom(), *handler_, jsonStr, errc);
        }};

    // ── Login (No Auth) ─────────────────────────────────────────────────
    ROUTE("/gtw/cwai/login/", kNoAuth, auth_handler_, Auth, DoLogin);
    ROUTE("/gtw/cwai/login/", kNoAuth, auth_handler_, Auth, ModifyPassword);
}

void ApiRouter::RegisterNetworkRoutes() {
    // ── Network ──────────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/network/", kAuth, network_handler_, Network, QueryNetCard);
    ROUTE("/gtw/cwai/network/", kAuth, network_handler_, Network, ModifyNetCard);
    ROUTE("/gtw/cwai/network/", kAuth, network_handler_, Network, QueryNetDns);
    ROUTE("/gtw/cwai/network/", kAuth, network_handler_, Network, ModifyNetDns);
    ROUTE("/gtw/cwai/network/", kAuth, network_handler_, Network, NetworkQualityCheck);
    ROUTE("/gtw/cwai/network/", kAuth, network_handler_, Network, IpAccessibleCheck);
}

void ApiRouter::RegisterAlgorithmRoutes() {
    // ── Algorithm Management ──────────────────────────────────────────────────────
    ROUTE("/gtw/cwai/Algorithm/", kAuth, algorithm_handler_, Algorithm, Page);
    ROUTE("/gtw/cwai/Algorithm/", kAuth, algorithm_handler_, Algorithm, Upload);
    ROUTE("/gtw/cwai/Algorithm/", kAuth, algorithm_handler_, Algorithm, Update);
    ROUTE("/gtw/cwai/Algorithm/", kAuth, algorithm_handler_, Algorithm, Delete);
    ROUTE("/gtw/cwai/Algorithm/", kAuth, algorithm_handler_, Algorithm, Add);
    ROUTE("/gtw/cwai/Algorithm/", kAuth, algorithm_handler_, Algorithm, PassFlowList);

    // ── Orchestration Algorithm (AIBox platform, URL and DTO name mismatch requires manual registration)
    // ──────────
    url_map_[util::ToLower("/gtw/cwai/algorithm/layout/save")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<Algorithm::MsgLayoutSaveSend, Algorithm::MsgLayoutSaveRecv>(
                GetMessageFrom(), *algorithm_handler_, jsonStr, errc);
        }};
    url_map_[util::ToLower("/gtw/cwai/algorithm/layout/detail")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<Algorithm::MsgLayoutDetailSend, Algorithm::MsgLayoutDetailRecv>(
                GetMessageFrom(), *algorithm_handler_, jsonStr, errc);
        }};
    url_map_[util::ToLower("/gtw/cwai/algorithm/layout/list")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<Algorithm::MsgLayoutListSend, Algorithm::MsgLayoutListRecv>(
                GetMessageFrom(), *algorithm_handler_, jsonStr, errc);
        }};
    url_map_[util::ToLower("/gtw/cwai/algorithm/layout/exportSingleAlg")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            std::string jsonResponse = detail::DispatchJson<Algorithm::MsgLayoutExportSingleSend,
                                                            Algorithm::MsgLayoutExportSingleRecv>(
                GetMessageFrom(), *algorithm_handler_, jsonStr, errc);
            return DispatchFileDownload(jsonResponse);
        }};

    // Export all orchestration algorithms (Special handling: return tar.gz file content for download)
    url_map_[util::ToLower("/gtw/cwai/algorithm/layout/export")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            std::string jsonResponse =
                detail::DispatchJson<Algorithm::MsgLayoutExportAllSend, Algorithm::MsgLayoutExportAllRecv>(
                    GetMessageFrom(), *algorithm_handler_, jsonStr, errc);
            return DispatchFileDownload(jsonResponse);
        }};

    // Note: This route URL does not follow layout/ prefix rule
    url_map_[util::ToLower("/gtw/cwai/atomic/action/list")] = {
        kAuth, [this](const std::string& jsonStr, std::error_condition& errc) {
            return detail::DispatchJson<Algorithm::MsgAtomicActionListSend,
                                        Algorithm::MsgAtomicActionListRecv>(
                GetMessageFrom(), *algorithm_handler_, jsonStr, errc);
        }};
}

// Route registration — moved to ApiRouterRoutes.cc

}  // namespace cosmo
