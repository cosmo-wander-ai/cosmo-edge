#include <algorithm>
#include <mutex>
#include <set>

#include "api/MessageCameraHandler.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/detail/ServiceRegistry.h"
#include "service/gb28181/IGb28181Management.h"
#include "service/gb28181/impl/GbSipProtocol.h"

namespace cosmo {
camera::MsgGb28181ManageSend MessageCameraHandler::Handle(camera::MsgGb28181ManageRecv&& data,
                                                          std::error_condition&) {
    camera::MsgGb28181ManageSend response;
    try {
        auto& management  = service::ServiceRegistry::Instance().Get<service::IGb28181Management>();
        const auto action = data.input.value("action", "list");
        if (action == "addChannel") {
            // Serialize this compound operation so retries cannot create duplicate logical sources.
            static std::mutex add_mtx;
            std::lock_guard<std::mutex> lock(add_mtx);
            const auto channel = data.input.at("channelId").get<std::string>();
            const auto name    = data.input.at("channelName").get<std::string>();
            if (!service::gb::IsId(channel) || name.empty() || name.size() > 256)
                throw std::runtime_error("invalid_parameter");
            size_t total       = 0;
            const auto cameras = crud_.Query("", -1, 1, 10000, total);
            const auto source  = "gb28181://" + channel;
            if (std::any_of(cameras.begin(), cameras.end(), [&](const auto& camera) {
                    return camera.channelType == MsgCameraType::MsgCameraTypeGb28181 && camera.url == source;
                }))
                throw std::runtime_error("duplicate_source");
            auto check      = data.input;
            check["action"] = "validateChannel";
            management.Execute(check);
            MsgCameraInfo camera;
            camera.channelType = MsgCameraType::MsgCameraTypeGb28181;
            camera.channelName = name;
            camera.url         = source;
            std::string id;
            if (crud_.Add(camera, id) != util::ErrorEnum::Success)
                throw std::runtime_error("channel_save_failed");
            management.Ensure(channel);
            response.resData = {{"id", id}, {"source", source}};
        } else {
            response.resData = management.Execute(data.input);
            if (action == "list") {
                size_t total       = 0;
                const auto cameras = crud_.Query("", -1, 1, 10000, total);
                std::set<std::string> saved;
                for (const auto& camera : cameras)
                    if (camera.channelType == MsgCameraType::MsgCameraTypeGb28181)
                        saved.insert(camera.url);
                for (auto& device : response.resData["devices"])
                    for (auto& channel : device["channels"])
                        channel["added"] = saved.count("gb28181://" + channel["id"].get<std::string>()) != 0;
            }
        }
        response.resData["success"] = true;
    } catch (const nlohmann::json::exception&) {
        response.resData = {{"success", false}, {"error", "invalid_parameter"}};
    } catch (const std::exception& error) {
        static const std::set<std::string> errors{
            "invalid_parameter",    "password_required", "device_offline",      "channel_not_found",
            "duplicate_channel_id", "duplicate_source",  "channel_save_failed", "busy",
            "device_limit",         "storage_error",     "listen_failed",       "realm_in_use",
            "service_unavailable"};
        response.resData = {{"success", false},
                            {"error", errors.count(error.what()) ? error.what() : "service_unavailable"}};
    }
    return response;
}
}  // namespace cosmo
