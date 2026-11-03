#include "api/MessageWifiHandler.h"

#include <stdexcept>

#include "nlohmann/json.hpp"
#include "util/ErrorCode.h"
#include "util/MsgBaseTypes.h"

namespace cosmo {
MessageWifiHandler::MessageWifiHandler(service::IWifiConfig& wifi) : wifi_(wifi) {}

std::string MessageWifiHandler::Handle(const std::string& action, const RequestDispatchContext& context,
                                       const std::string& body, std::error_condition& error) {
    nlohmann::json result;
    std::string failure;
    try {
        if (context.transport != RequestTransport::kHttp || context.http_method != "POST") {
            throw std::runtime_error("Wi-Fi management requires an authenticated HTTP POST");
        }
        const auto request = body.empty() ? nlohmann::json::object() : nlohmann::json::parse(body);
        if (!request.is_object()) {
            throw std::runtime_error("Invalid Wi-Fi request");
        }
        if (action == "queryNetworkStatus") {
            result = wifi_.QueryStatus();
        } else if (action == "scanWifi") {
            result = wifi_.Scan();
        } else if (action == "connectWifi") {
            result = wifi_.Connect(request);
        } else if (action == "queryWifiOperation") {
            result = wifi_.QueryOperation(request.at("operationId").get<std::string>());
        } else if (action == "confirmWifiConfig") {
            result = wifi_.Confirm(request.at("operationId").get<std::string>());
        } else if (action == "disconnectWifi") {
            result = wifi_.Disconnect();
        } else if (action == "forgetWifi") {
            result = wifi_.Forget();
        } else {
            throw std::runtime_error("Unknown Wi-Fi operation");
        }
        error = util::ErrorEnum::Success;
    } catch (const nlohmann::json::exception&) {
        // JSON diagnostics can contain the write-only password. Never expose them.
        failure = "Invalid Wi-Fi request";
        error   = util::ErrorEnum::ParameterException;
    } catch (const std::exception& exception) {
        failure = exception.what();
        error   = util::ErrorEnum::Failed;
    }
    MsgSendHead head;
    head.resCode = failure.empty() ? kServerRspSuccess : kServerRspFailed;
    MsgResBase message;
    message.msgCode    = std::to_string(error.value());
    message.messageKey = "api.error." + util::ErrorEnumName(static_cast<util::ErrorEnum>(error.value()));
    message.msgText    = failure.empty() ? "Success" : failure;
    head.resMsg.push_back(message);
    nlohmann::json response = head;
    if (failure.empty()) {
        response["resData"] = std::move(result);
    }
    return response.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
}  // namespace cosmo
