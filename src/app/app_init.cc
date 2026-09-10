// app_init — app_init implementation.

#include "app/app_init.h"

#include <cassert>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>

#include "RuntimePathsConfig.h"
#include "api/ApiRouter.h"
#include "app/AppConstants.h"
#include "media/IOsdTextRenderer.h"
#include "media/OsdTextRenderer.h"
#include "service/ai/impl/InferPoolServiceImpl.h"
#include "service/ai/impl/LlmInferServiceImpl.h"
#include "service/algorithm/IAlgorithmCrud.h"
#include "service/algorithm/IAlgorithmLayout.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/algorithm/IAlgorithmService.h"
#include "service/algorithm/impl/ActionServiceImpl.h"
#include "service/algorithm/impl/AlgorithmServiceImpl.h"
#include "service/camera/ICameraChannelQuery.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmPushService.h"
#include "service/event/IEventNotifier.h"
#include "service/event/impl/AlarmPushServiceImpl.h"
#include "service/event/impl/AlarmRecordServiceImpl.h"
#include "service/event/impl/EventNotifierImpl.h"
#include "service/face/IArticlesReidDaoService.h"
#include "service/face/IBodyLibService.h"
#include "service/face/IFaceFeature.h"
#include "service/face/IFaceImport.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IFaceLibService.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRecogDaoService.h"
#include "service/face/IPersonRepo.h"
#include "service/face/impl/ArticlesReidDaoServiceImpl.h"
#include "service/face/impl/BodyLibServiceImpl.h"
#include "service/face/impl/FaceLibServiceImpl.h"
#include "service/face/impl/PersonDaoServiceImpl.h"
#include "service/face/impl/PersonRecogDaoServiceImpl.h"
#include "service/gb28181/IGb28181SourceService.h"
#include "service/gb28181/impl/Gb28181SourceServiceImpl.h"
#include "service/infra/IDbService.h"
#include "service/infra/ILinkageService.h"
#include "service/infra/IMemoryPoolService.h"
#include "service/infra/impl/DbServiceImpl.h"
#include "service/infra/impl/LinkageServiceImpl.h"
#include "service/infra/impl/MemoryPoolServiceImpl.h"
#include "service/infra/impl/StorageCleanServiceImpl.h"
#include "service/media/IPicTaskDetect.h"
#include "service/media/IPicTaskLifecycle.h"
#include "service/media/IPicTaskQuery.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameService.h"
#include "service/media/IVideoFrameTransform.h"
#include "service/media/impl/AudioServiceImpl.h"
#include "service/media/impl/LiveStreamServiceImpl.h"
#include "service/media/impl/PicTaskServiceImpl.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelQuery.h"
#include "service/model/IModelService.h"
#include "service/model/impl/ModelServiceImpl.h"
#include "service/modelguard/IModelAuthorizationService.h"
#include "service/modelguard/impl/ModelAuthorizationServiceImpl.h"
#include "service/network/INetworkService.h"
#include "service/network/impl/AuthServiceImpl.h"
#include "service/network/impl/ClientMessageServiceImpl.h"
#include "service/network/impl/DeviceDiscoveryServiceImpl.h"
#include "service/network/impl/HttpClientImpl.h"
#include "service/network/impl/NetworkServiceImpl.h"
#include "service/onboarding/IOnboardingService.h"
#include "service/onboarding/impl/OnboardingServiceImpl.h"
#include "service/onvif/impl/OnvifServiceImpl.h"
#include "service/path/IUploadStagingService.h"
#include "service/path/impl/FileServiceImpl.h"
#include "service/path/impl/UploadStagingServiceImpl.h"
#include "service/system/IConfigNetworkService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IConfigWriteService.h"
#include "service/system/IDeviceHardware.h"
#include "service/system/IDeviceInfoService.h"
#include "service/system/IHardwareQuery.h"
#include "service/system/IMemoryDiag.h"
#include "service/system/IOverviewConfig.h"
#include "service/system/ITimerRestartService.h"
#include "service/system/impl/AppInfoServiceImpl.h"
#include "service/system/impl/DeviceInfoServiceImpl.h"
#include "service/system/impl/HardwareQueryUtil.h"
#include "service/system/impl/SystemOperationServiceImpl.h"
#include "service/system/impl/SystemServiceImpl.h"
#include "service/system/impl/TimeServiceImpl.h"
#include "service/system/impl/TimerRestartServiceImpl.h"
#include "service/system/impl/WatchDogServiceImpl.h"
#include "service/task/ITaskChannel.h"
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskQuery.h"
#include "service/task/ITaskService.h"
#include "service/task/impl/ScheduleServiceImpl.h"
#include "service/task/impl/TaskServiceImpl.h"
#include "util/EnvUtil.h"
#include "util/Log.h"
#include "util/NnBackendConstants.h"
#include "util/PathUtil.h"

namespace cosmo::app {

void SwDevicePreInit() {
    cosmo::path::OverrideRootPaths(
        cosmo::util::GetEnvOrDefault("COSMO_DATA_DIR", cosmo::runtime::kDefaultDataDir),
        cosmo::util::GetEnvOrDefault("COSMO_APP_DATA_DIR", cosmo::runtime::kDefaultAppDataDir));
    cosmo::path::Init();
}

static void RegisterInfrastructureServices() {
    auto& registry = cosmo::service::ServiceRegistry::Instance();

    registry.Register<cosmo::service::IFileService>(std::make_unique<cosmo::service::FileServiceImpl>());
    registry.Register<cosmo::service::IUploadStagingService>(
        std::make_unique<cosmo::service::UploadStagingServiceImpl>());

    auto eventNotifier = std::make_unique<cosmo::service::EventNotifierImpl>();
    registry.Register<cosmo::service::IEventNotifier>(std::move(eventNotifier));

    registry.Register<cosmo::service::IMemoryPoolService>(
        std::make_unique<cosmo::service::MemoryPoolServiceImpl>());

    registry.Register<cosmo::service::IStorageCleanService>(
        std::make_unique<cosmo::service::StorageCleanServiceImpl>());

    registry.Register<cosmo::service::IWatchDogService>(
        std::make_unique<cosmo::service::WatchDogServiceImpl>());

    registry.Register<cosmo::service::IDbService>(std::make_unique<cosmo::service::DbServiceImpl>());

    // OSD TrueType text renderer (Source Han Sans) — must be registered before IVideoFrameService,
    // because VideoFrameServiceImpl → VideoFrameProc → VideoFrameProcSophon needs IOsdTextRenderer.
    registry.Register<cosmo::media::IOsdTextRenderer>(std::make_unique<cosmo::media::OsdTextRenderer>());

    registry.Register<cosmo::service::IVideoFrameService>(
        std::make_unique<cosmo::service::VideoFrameServiceImpl>());
    // ISP split: VideoFrameServiceImpl implements 3 narrow interfaces.
    // Register under IVideoFrameService (owning), then alias the sub-interfaces.
    auto& videoFrameImpl = registry.Get<cosmo::service::IVideoFrameService>();
    registry.Set<cosmo::service::IVideoFrameOSD>(
        static_cast<cosmo::service::IVideoFrameOSD*>(&videoFrameImpl));
    registry.Set<cosmo::service::IVideoFrameTransform>(
        static_cast<cosmo::service::IVideoFrameTransform*>(&videoFrameImpl));
    registry.Set<cosmo::service::IVideoFrameCodec>(
        static_cast<cosmo::service::IVideoFrameCodec*>(&videoFrameImpl));

    registry.Register<cosmo::service::ITaskService>(std::make_unique<cosmo::service::TaskServiceImpl>());
    auto& taskImpl = registry.Get<cosmo::service::ITaskService>();
    registry.Set<cosmo::service::ITaskLifecycle>(static_cast<cosmo::service::ITaskLifecycle*>(&taskImpl));
    registry.Set<cosmo::service::ITaskQuery>(static_cast<cosmo::service::ITaskQuery*>(&taskImpl));
    registry.Set<cosmo::service::ITaskChannel>(static_cast<cosmo::service::ITaskChannel*>(&taskImpl));

    registry.Register<cosmo::service::IInferPoolService>(
        std::make_unique<cosmo::service::InferPoolServiceImpl>());
    registry.Register<cosmo::service::ILlmInferService>(
        std::make_unique<cosmo::service::LlmInferServiceImpl>());

    registry.Register<cosmo::service::INetworkService>(std::make_unique<cosmo::service::NetworkServiceImpl>(
        []() { return std::make_unique<cosmo::ApiRouter>(cosmo::MessageFromType::MessageFromHttp); },
        []() { return std::make_unique<cosmo::ApiRouter>(cosmo::MessageFromType::MessageFromMqtt); }));

    registry.Register<cosmo::service::IDeviceDiscoveryService>(
        std::make_unique<cosmo::service::DeviceDiscoveryServiceImpl>());

    registry.Register<cosmo::service::IHttpClient>(std::make_unique<cosmo::service::HttpClientImpl>());
    registry.Register<cosmo::service::IGb28181SourceService>(
        std::make_unique<cosmo::service::Gb28181SourceServiceImpl>());
    registry.Register<cosmo::service::IOnvifService>(std::make_unique<cosmo::service::OnvifServiceImpl>());
}

static void RegisterBusinessServices() {
    auto& registry = cosmo::service::ServiceRegistry::Instance();

    registry.Register<cosmo::service::IAudioService>(std::make_unique<cosmo::service::AudioServiceImpl>());
    registry.Register<cosmo::service::ILinkageService>(
        std::make_unique<cosmo::service::LinkageServiceImpl>());
    registry.Register<cosmo::service::ICameraService>(std::make_unique<cosmo::service::CameraServiceImpl>());
    // ISP split: CameraServiceImpl implements 3 narrow interfaces.
    // Register under ICameraService (owning), then alias the sub-interfaces.
    auto& cameraImpl = registry.Get<cosmo::service::ICameraService>();
    registry.Set<cosmo::service::ICameraDeviceCrud>(
        static_cast<cosmo::service::ICameraDeviceCrud*>(&cameraImpl));
    registry.Set<cosmo::service::ICameraTaskConfig>(
        static_cast<cosmo::service::ICameraTaskConfig*>(&cameraImpl));
    registry.Set<cosmo::service::ICameraChannelQuery>(
        static_cast<cosmo::service::ICameraChannelQuery*>(&cameraImpl));

    registry.Register<cosmo::service::IPicTaskService>(
        std::make_unique<cosmo::service::PicTaskServiceImpl>());
    // ISP split: PicTaskServiceImpl implements 3 narrow interfaces.
    // Register under IPicTaskService (owning), then alias the sub-interfaces.
    auto& picTaskImpl = registry.Get<cosmo::service::IPicTaskService>();
    registry.Set<cosmo::service::IPicTaskLifecycle>(
        static_cast<cosmo::service::IPicTaskLifecycle*>(&picTaskImpl));
    registry.Set<cosmo::service::IPicTaskDetect>(static_cast<cosmo::service::IPicTaskDetect*>(&picTaskImpl));
    registry.Set<cosmo::service::IPicTaskQuery>(static_cast<cosmo::service::IPicTaskQuery*>(&picTaskImpl));

    registry.Register<cosmo::service::IAlgorithmService>(
        std::make_unique<cosmo::service::AlgorithmServiceImpl>());
    // ISP split: AlgorithmServiceImpl implements 3 narrow interfaces.
    // Register under IAlgorithmService (owning), then alias the sub-interfaces.
    auto& algImpl = registry.Get<cosmo::service::IAlgorithmService>();
    registry.Set<cosmo::service::IAlgorithmQuery>(static_cast<cosmo::service::IAlgorithmQuery*>(&algImpl));
    registry.Set<cosmo::service::IAlgorithmCrud>(static_cast<cosmo::service::IAlgorithmCrud*>(&algImpl));
    registry.Set<cosmo::service::IAlgorithmLayout>(static_cast<cosmo::service::IAlgorithmLayout*>(&algImpl));

    registry.Register<cosmo::service::IDeviceInfoService>(
        std::make_unique<cosmo::service::DeviceInfoServiceImpl>());
    auto& deviceInfo = registry.Get<cosmo::service::IDeviceInfoService>();
    registry.Set<cosmo::service::IDeviceHardware>(static_cast<cosmo::service::IDeviceHardware*>(&deviceInfo));
    registry.Register<cosmo::service::ITimeService>(std::make_unique<cosmo::service::TimeServiceImpl>());
    // ISP split: SystemServiceImpl implements 3 narrow interfaces.
    // Register under IConfigReadService (owning), then alias the other two.
    // Use dynamic_cast to safely verify the implementation type at registration time.
    registry.Register<cosmo::service::IConfigReadService>(
        std::make_unique<cosmo::service::SystemServiceImpl>());
    auto* sysConfigPtr =
        dynamic_cast<cosmo::service::SystemServiceImpl*>(&registry.Get<cosmo::service::IConfigReadService>());
    assert(sysConfigPtr && "IConfigReadService must be backed by SystemServiceImpl");
    registry.Set<cosmo::service::IConfigWriteService>(
        static_cast<cosmo::service::IConfigWriteService*>(sysConfigPtr));
    registry.Set<cosmo::service::IConfigNetworkService>(
        static_cast<cosmo::service::IConfigNetworkService*>(sysConfigPtr));
    registry.Register<cosmo::service::ISystemOperationService>(
        std::make_unique<cosmo::service::SystemOperationServiceImpl>());
    registry.Register<cosmo::service::IModelService>(std::make_unique<cosmo::service::ModelServiceImpl>());
    registry.Register<cosmo::service::IModelAuthorizationService>(
        std::make_unique<cosmo::service::ModelAuthorizationServiceImpl>());
    auto& modelImpl = registry.Get<cosmo::service::IModelService>();
    registry.Set<cosmo::service::IModelQuery>(static_cast<cosmo::service::IModelQuery*>(&modelImpl));
    registry.Set<cosmo::service::IModelPathMapping>(
        static_cast<cosmo::service::IModelPathMapping*>(&modelImpl));

    registry.Register<cosmo::service::IAlarmRecordService>(
        std::make_unique<cosmo::service::AlarmRecordServiceImpl>());

    registry.Register<cosmo::service::IAlarmPushService>(
        std::make_unique<cosmo::service::AlarmPushServiceImpl>());

    registry.Register<cosmo::service::IAuthService>(std::make_unique<cosmo::service::AuthServiceImpl>());
    registry.Register<cosmo::service::IScheduleService>(
        std::make_unique<cosmo::service::ScheduleServiceImpl>());
    registry.Register<cosmo::service::IFaceLibService>(
        std::make_unique<cosmo::service::FaceLibServiceImpl>());
    auto& faceLibImpl = registry.Get<cosmo::service::IFaceLibService>();

    registry.Set<cosmo::service::IFaceLibRepo>(static_cast<cosmo::service::IFaceLibRepo*>(&faceLibImpl));
    registry.Set<cosmo::service::IPersonRepo>(static_cast<cosmo::service::IPersonRepo*>(&faceLibImpl));

    registry.Set<cosmo::service::IFaceFeature>(static_cast<cosmo::service::IFaceFeature*>(&faceLibImpl));
    registry.Set<cosmo::service::IFaceImport>(static_cast<cosmo::service::IFaceImport*>(&faceLibImpl));
    registry.Register<cosmo::service::IBodyLibService>(
        std::make_unique<cosmo::service::BodyLibServiceImpl>());
    registry.Register<cosmo::service::IPersonDaoService>(
        std::make_unique<cosmo::service::PersonDaoServiceImpl>());
    registry.Register<cosmo::service::IArticlesReidDaoService>(
        std::make_unique<cosmo::service::ArticlesReidDaoServiceImpl>());
    registry.Register<cosmo::service::IPersonRecogDaoService>(
        std::make_unique<cosmo::service::PersonRecogDaoServiceImpl>());
    registry.Register<cosmo::service::ILiveStreamService>(
        std::make_unique<cosmo::service::LiveStreamServiceImpl>());
    registry.Register<cosmo::service::IOnboardingService>(
        std::make_unique<cosmo::service::OnboardingServiceImpl>());
    registry.Register<cosmo::service::IActionService>(std::make_unique<cosmo::service::ActionServiceImpl>());
    registry.Register<cosmo::service::IClientMessageService>(
        std::make_unique<cosmo::service::ClientMessageServiceImpl>());

    auto appInfoService = std::make_unique<cosmo::service::AppInfoServiceImpl>();
    appInfoService->SetDevId(registry.Get<cosmo::service::IDeviceHardware>().GetDevSn());
    appInfoService->SetEngineType(cosmo::util::kEngineType);
    registry.Register<cosmo::service::IAppInfoService>(std::move(appInfoService));
    // ISP split: AppInfoServiceImpl implements 3 narrow interfaces.
    // Register under IAppInfoService (owning), then alias the other three.
    auto& appInfoImpl = registry.Get<cosmo::service::IAppInfoService>();
    registry.Set<cosmo::service::IOverviewConfig>(
        static_cast<cosmo::service::IOverviewConfig*>(&appInfoImpl));
    registry.Set<cosmo::service::IHardwareQuery>(static_cast<cosmo::service::IHardwareQuery*>(&appInfoImpl));
    registry.Set<cosmo::service::IMemoryDiag>(static_cast<cosmo::service::IMemoryDiag*>(&appInfoImpl));

    registry.Register<cosmo::service::ITimerRestartService>(
        std::make_unique<cosmo::service::TimerRestartServiceImpl>());
}

static void InitializeServices() {
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    try {
        registry.Get<cosmo::service::IOnvifService>().Init();
    } catch (const std::exception&) {
        LOG_ERRO("{}", "ONVIF configuration unavailable; restore its configuration and key together");
    }

    // OSD TrueType text renderer initialization
    auto& osd                               = registry.Get<cosmo::media::IOsdTextRenderer>();
    constexpr std::string_view kFontPaths[] = {
        "../font/SOURCEHANSANSCN-REGULAR.OTF",                             // deploy: bin/../font/
        "/data/cosmo-engine-deploy/font/SOURCEHANSANSCN-REGULAR.OTF",      // device deploy dir
        "/appfs/cosmo_wander/cwai_data/font/SOURCEHANSANSCN-REGULAR.OTF",  // device system font
        "data/SOURCEHANSANSCN-REGULAR.OTF",                                // dev env (project root)
    };
    bool fontLoaded = false;
    for (const auto& fp : kFontPaths) {
        if (osd.Init(std::string(fp))) {
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
        LOG_WARN("{}", "OSD TrueType font not found, falling back to bmcv text rendering");
    }

    // AlgorithmService::Init() must run after ALL services are registered,
    // because it may depend on services registered later in the sequence.
    registry.Get<cosmo::service::IAlgorithmService>().Init();

    registry.Get<cosmo::service::INetworkService>().Init();

    registry.Get<cosmo::service::IDbService>().Init();

    registry.Get<cosmo::service::IAlarmPushService>().Init();

    registry.Get<cosmo::service::IModelService>().Init();
    registry.Get<cosmo::service::IDeviceInfoService>().GetCpuUtilization();
    registry.Get<cosmo::service::ITimerRestartService>().Start();
    registry.Get<cosmo::service::IFaceLibService>().LoadFaceData();
    registry.Get<cosmo::service::ICameraService>().InitCameraEntities();
    registry.Get<cosmo::service::IAppInfoService>().SetOverviewStructureRecord(true);

    registry.Get<cosmo::service::INetworkService>().MqttStart();
}

static void InitializeExternalComponents() {
    using namespace constants;

    // Storage space periodic cleanup
    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IStorageCleanService>().Start();

    // HTTP Server Init
    const int http_port = cosmo::util::GetEnvIntOrDefault("COSMO_HTTP_PORT", kDefaultHttpPort);
    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::INetworkService>().InitHttpServer(
        "0.0.0.0", static_cast<uint16_t>(http_port));

    // uWebSockets Server Init
    const int websocket_port = cosmo::util::GetEnvIntOrDefault("COSMO_WEBSOCKET_PORT", kDefaultWebSocketPort);
    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().InitializeWebSocket(
        "0.0.0.0", websocket_port);

    // Device discovery multicast service (async init with retry).
    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IDeviceDiscoveryService>().Start();

#ifndef COSMO_DEV_MODE
    // Hardware watchdog — feeds /dev/watchdog to prevent system reset on hang.
    // Disabled in development builds (COSMO_DEV_MODE) to avoid device resets during debugging.
    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IWatchDogService>().Start();
#else
    LOG_WARN("{}", "Watchdog disabled (COSMO_DEV_MODE build)");
#endif
}

static void StopExternalComponents() {
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    auto stop      = [](const char* name, auto&& callback) {
        try {
            callback();
        } catch (const std::exception& ex) {
            LOG_ERRO("Failed to stop {}: {}", name, ex.what());
        } catch (...) {
            LOG_ERRO("Failed to stop {}: unknown error", name);
        }
    };

    // Stop every external caller before ServiceRegistry::ShutdownAll(). Get()
    // returns non-owning references, so registry destruction is only safe after
    // ingress and background producers have joined their worker threads.
    if (registry.Has<cosmo::service::IWatchDogService>()) {
        stop("hardware watchdog",
             [&registry]() { static_cast<void>(registry.Get<cosmo::service::IWatchDogService>().Stop()); });
    }
    if (registry.Has<cosmo::service::INetworkService>()) {
        stop("HTTP server",
             [&registry]() { registry.Get<cosmo::service::INetworkService>().StopHttpServer(); });
        stop("MQTT client",
             [&registry]() { registry.Get<cosmo::service::INetworkService>().MqttShutdown(); });
        stop("network configuration worker",
             [&registry]() { registry.Get<cosmo::service::INetworkService>().StopAsyncApply(); });
    }
    if (registry.Has<cosmo::service::IDeviceDiscoveryService>()) {
        stop("device discovery",
             [&registry]() { registry.Get<cosmo::service::IDeviceDiscoveryService>().Stop(); });
    }
    if (registry.Has<cosmo::service::ITimerRestartService>()) {
        stop("scheduled restart timer",
             [&registry]() { registry.Get<cosmo::service::ITimerRestartService>().Stop(); });
    }
    if (registry.Has<cosmo::service::ILiveStreamService>()) {
        stop("live stream workers",
             [&registry]() { registry.Get<cosmo::service::ILiveStreamService>().Stop(); });
    }
    if (registry.Has<cosmo::service::ICameraService>()) {
        stop("camera background workers",
             [&registry]() { registry.Get<cosmo::service::ICameraService>().Stop(); });
    }
    if (registry.Has<cosmo::service::IPicTaskLifecycle>()) {
        stop("picture task workers",
             [&registry]() { registry.Get<cosmo::service::IPicTaskLifecycle>().TaskDeleteAll(); });
    }
    if (registry.Has<cosmo::service::ITaskLifecycle>()) {
        stop("task workers", [&registry]() { registry.Get<cosmo::service::ITaskLifecycle>().Shutdown(); });
    }
    if (registry.Has<cosmo::service::ILinkageService>()) {
        stop("linkage workers", [&registry]() { registry.Get<cosmo::service::ILinkageService>().Stop(); });
    }
    if (registry.Has<cosmo::service::IFaceImport>()) {
        stop("face import and feature workers",
             [&registry]() { registry.Get<cosmo::service::IFaceImport>().Stop(); });
    }
    if (registry.Has<cosmo::service::IAlarmPushService>()) {
        stop("alarm push workers",
             [&registry]() { registry.Get<cosmo::service::IAlarmPushService>().Stop(); });
    }
    if (registry.Has<cosmo::service::IEventNotifier>()) {
        stop("WebSocket server",
             [&registry]() { registry.Get<cosmo::service::IEventNotifier>().ShutdownWebSocket(); });
    }
    if (registry.Has<cosmo::service::IStorageCleanService>()) {
        stop("storage cleaner",
             [&registry]() { registry.Get<cosmo::service::IStorageCleanService>().Stop(); });
    }
}

void SwDeviceInit() {
    RegisterInfrastructureServices();
    RegisterBusinessServices();
    cosmo::service::ServiceRegistry::Instance().CompleteRegistration();
    InitializeServices();
    InitializeExternalComponents();
}

void SwDeviceRun() {
    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::INetworkService>().RunHttpLoop();
}

void SwDeviceDestroy() {
    StopExternalComponents();

    // All externally driven work has stopped. It is now safe to invalidate
    // registry lookups and destroy owned services in reverse registration order.
    cosmo::service::ServiceRegistry::Instance().ShutdownAll();
}

}  // namespace cosmo::app
