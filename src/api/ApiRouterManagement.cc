#include "api/ApiRouterInternal.h"
#include "service/detail/ServiceRegistry.h"
#include "service/management/ManagementService.h"

namespace cosmo {
void ApiRouter::RegisterManagementRoutes() {
    for (const char* method : {"capabilities", "resourceinventory", "prepareresource", "uploadchunk",
                               "operationstatus", "applyoperation", "taskactivate"}) {
        url_map_[std::string("/gtw/cwai/management/") + method] = {
            kAuth,
            {},
            [method](const RequestDispatchContext& context, const std::string& body,
                     std::error_condition& errc) {
                try {
                    auto data =
                        service::ServiceRegistry::Instance().Get<service::IManagementService>().Handle(
                            method, context, body);
                    errc = util::ErrorEnum::Success;
                    return nlohmann::json(
                               {{"resCode", 1}, {"resData", data}, {"resMsg", nlohmann::json::array()}})
                        .dump();
                } catch (const service::ManagementError& error) {
                    errc = util::ErrorEnum::InvalidParam;
                    return nlohmann::json(
                               {{"resCode", 0},
                                {"resMsg", {{{"msgCode", error.what()}, {"msgText", error.what()}}}}})
                        .dump();
                } catch (const std::exception&) {
                    // Native errors may contain stream credentials or local paths.
                    errc = util::ErrorEnum::InternalError;
                    return nlohmann::json({{"resCode", 0},
                                           {"resMsg",
                                            {{{"msgCode", "MANAGEMENT_FAILED"},
                                              {"msgText", "Management request failed"}}}}})
                        .dump();
                }
            }};
    }
}
}  // namespace cosmo
