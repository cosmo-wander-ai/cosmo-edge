#include <algorithm>

#include "api/MessageCameraHandler.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/detail/ServiceRegistry.h"
#include "service/onvif/IOnvifService.h"
#include "util/ErrorCode.h"

namespace cosmo {
namespace {
    service::IOnvifService& Onvif() {
        return service::ServiceRegistry::Instance().Get<service::IOnvifService>();
    }
    template <typename Function>
    camera::OnvifResponse Execute(Function function) {
        camera::OnvifResponse response;
        try {
            response.resData            = function();
            response.resData["success"] = true;
        } catch (const nlohmann::json::exception&) {
            response.resData = {{"success", false}, {"error", "invalid_parameter"}};
        } catch (const std::exception& error) {
            // Unexpected library/filesystem errors must not expose request or credential data.
            static const std::vector<std::string> codes{"invalid_parameter",
                                                        "invalid_endpoint",
                                                        "no_interface",
                                                        "no_profiles",
                                                        "no_profile_selected",
                                                        "duplicate_source",
                                                        "busy",
                                                        "storage_error",
                                                        "source_not_found",
                                                        "source_limit",
                                                        "service_unavailable",
                                                        "soap_fault",
                                                        "invalid_xml",
                                                        "unauthorized",
                                                        "timeout",
                                                        "network_error",
                                                        "crypto_error",
                                                        "unsupported_stream",
                                                        "channel_save_failed"};
            const std::string code = error.what();
            response.resData       = {
                {"success", false},
                {"error",
                 std::find(codes.begin(), codes.end(), code) != codes.end() ? code : "service_unavailable"}};
        }
        return response;
    }
}  // namespace
camera::OnvifResponse MessageCameraHandler::Handle(camera::MsgOnvifInterfacesRecv&&, std::error_condition&) {
    return Execute([] { return nlohmann::json{{"rows", Onvif().Interfaces()}}; });
}
camera::OnvifResponse MessageCameraHandler::Handle(camera::MsgOnvifDiscoverRecv&& data,
                                                   std::error_condition&) {
    return Execute([&] {
        return nlohmann::json{{"rows", Onvif().Discover(data.input.value("interfaceAddress", ""),
                                                        data.input.value("timeoutMs", 2500))}};
    });
}
camera::OnvifResponse MessageCameraHandler::Handle(camera::MsgOnvifProbeRecv&& data, std::error_condition&) {
    return Execute([&] { return Onvif().Probe(data.input); });
}
camera::OnvifResponse MessageCameraHandler::Handle(camera::MsgOnvifGetRecv&& data, std::error_condition&) {
    return Execute([&] { return Onvif().Describe(data.input.at("source")); });
}
camera::OnvifResponse MessageCameraHandler::Handle(camera::MsgOnvifSaveRecv&& data, std::error_condition&) {
    return Execute([&] {
        MsgCameraInfo config;
        config.channelType = MsgCameraType::MsgCameraTypeOnvif;
        config.channelName = data.input.at("channelName");
        if (config.channelName.empty() || config.channelName.size() > 256)
            throw std::runtime_error("invalid_parameter");
        const auto camera_id = data.input.value("videoChannelId", "");
        std::string source;
        if (!camera_id.empty()) {
            size_t total       = 0;
            const auto cameras = crud_.Query("", -1, 1, 10000, total);
            auto it            = std::find_if(cameras.begin(), cameras.end(), [&](const auto& camera) {
                return camera.videoChannelId == camera_id &&
                       camera.channelType == MsgCameraType::MsgCameraTypeOnvif;
            });
            if (it == cameras.end())
                throw std::runtime_error("source_not_found");
            config             = *it;
            config.channelName = data.input.at("channelName");
            source             = config.url;
        }
        config.url = Onvif().Save(data.input, source);
        if (camera_id.empty()) {
            std::string id;
            const auto error = crud_.Add(config, id);
            if (error != util::ErrorEnum::Success) {
                Onvif().Remove(config.url);
                throw std::runtime_error("channel_save_failed");
            }
            return nlohmann::json{{"id", id}, {"source", config.url}};
        }
        if (crud_.Update(config) != util::ErrorEnum::Success)
            throw std::runtime_error("channel_save_failed");
        return nlohmann::json{{"id", camera_id}, {"source", config.url}};
    });
}
}  // namespace cosmo
