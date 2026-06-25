// ApiRouter.h — API route dispatcher.
//
// Implementation partitions (methods declared here, defined in separate .cc):
//   ApiRouterRoutes.cc  — route registration for all modules

#pragma once

#include <mutex>
#include <shared_mutex>
#include <thread>

#include "api/MessageAlgorithmHandler.h"
#include "api/MessageAudioHandler.h"
#include "api/MessageAuthHandler.h"
#include "api/MessageBodyLibHandler.h"
#include "api/MessageCameraHandler.h"
#include "api/MessageEventHandler.h"
#include "api/MessageFaceLibHandler.h"
#include "api/MessageHandler.h"
#include "api/MessageImportFileHandler.h"
#include "api/MessageLinkageHandler.h"
#include "api/MessageLiveStreamHandler.h"
#include "api/MessageModelHandler.h"
#include "api/MessageNetworkHandler.h"
#include "api/MessageScheduleHandler.h"
#include "api/MessageSystemHandler.h"
#include "api/MessageThingsLibHandler.h"
#include "api/MessageOnboardingHandler.h"
#include "api/MessageVideoTaskHandler.h"
#include "util/IRequestDispatcher.h"

namespace cosmo {

enum class InterfaceMsgAuthType { None = 0, Mtk, Appkey };
struct InterfaceMsgMapUnit {
    InterfaceMsgAuthType authType{InterfaceMsgAuthType::None};
    std::function<std::string(const std::string&, std::error_condition&)> func;
};
// Main thread
class ApiRouter : public IRequestDispatcher {
public:
    explicit ApiRouter(MessageFromType from);
    ~ApiRouter() override = default;

    ApiRouter(const ApiRouter&)            = delete;
    ApiRouter& operator=(const ApiRouter&) = delete;

    bool SupportsRoute(const std::string& interface) override;
    bool DispatchRequest(const std::string& interface, const std::string& mtk, const std::string& message,
                         std::string& response) override;

    // Legacy overload without mtk — used by MQTT caller
    bool DispatchRequest(const std::string& interface, const std::string& message, std::string& response);
    MessageFromType GetMessageFrom() const {
        return from_;
    };

    MessageHandler& Handler();

private:
    bool MtkValid(const std::string& mtk, InterfaceMsgAuthType interfaceAuthType);

    void RegisterCoreRoutes();
    void RegisterNetworkRoutes();
    void RegisterAlgorithmRoutes();
    void RegisterModelRoutes();
    void RegisterScheduleRoutes();
    void RegisterEventRoutes();
    void RegisterCameraRoutes();
    void RegisterTaskRoutes();
    void RegisterSystemRoutes();
    void RegisterLibraryRoutes();
    void RegisterFileRoutes();
    void RegisterAudioRoutes();
    void RegisterLinkageRoutes();
    void RegisterLiveStreamRoutes();
    void RegisterOnboardingRoutes();

    std::string DispatchFileDownload(const std::string& jsonResponse);

    std::unique_ptr<MessageHandler> handler_;
    std::unique_ptr<MessageAuthHandler> auth_handler_;
    std::unique_ptr<MessageNetworkHandler> network_handler_;
    std::unique_ptr<MessageAlgorithmHandler> algorithm_handler_;
    std::unique_ptr<MessageModelHandler> model_handler_;
    std::unique_ptr<MessageScheduleHandler> schedule_handler_;
    std::unique_ptr<MessageEventHandler> event_handler_;
    std::unique_ptr<MessageCameraHandler> camera_handler_;
    std::unique_ptr<MessageVideoTaskHandler> video_task_handler_;
    std::unique_ptr<MessageSystemHandler> system_handler_;
    std::unique_ptr<MessageLiveStreamHandler> live_stream_handler_;
    std::unique_ptr<MessageFaceLibHandler> lib_handler_;
    std::unique_ptr<MessageImportFileHandler> import_file_handler_;
    std::unique_ptr<MessageBodyLibHandler> body_lib_handler_;
    std::unique_ptr<MessageThingsLibHandler> things_lib_handler_;
    std::unique_ptr<MessageAudioHandler> audio_handler_;
    std::unique_ptr<MessageLinkageHandler> linkage_handler_;
    std::unique_ptr<MessageOnboardingHandler> onboarding_handler_;
    std::map<std::string, InterfaceMsgMapUnit> url_map_;
    MessageFromType from_{MessageFromType::MessageFromHttp};
};
}  // namespace cosmo
