/// @file MessageOnboardingHandler.cc
/// @brief Handler implementation for Onboarding Wizard REST API.
#include "api/MessageOnboardingHandler.h"

#include "service/onboarding/IOnboardingService.h"
#include "util/ErrorCode.h"
#include "util/Log.h"

namespace cosmo {

MessageOnboardingHandler::MessageOnboardingHandler(service::IOnboardingService& onboarding_service)
    : onboarding_service_(onboarding_service) {}

Onboarding::MsgStatusSend MessageOnboardingHandler::Handle(Onboarding::MsgStatusRecv&& data,
                                                            std::error_condition& errc) {
    (void)data;
    Onboarding::MsgStatusSend ret{};
    ret.res_data.onboarding_completed = onboarding_service_.IsOnboardingCompleted();
    errc = util::ErrorEnum::Success;
    return ret;
}

Onboarding::MsgStartDemoSend MessageOnboardingHandler::Handle(Onboarding::MsgStartDemoRecv&& data,
                                                               std::error_condition& errc) {
    (void)data;
    Onboarding::MsgStartDemoSend ret{};
    auto result = onboarding_service_.StartDemo();
    ret.res_data.camera_id      = result.camera_id;
    ret.res_data.camera_name    = result.camera_name;
    ret.res_data.algorithm_code = result.algorithm_code;
    errc = util::ErrorEnum::Success;
    return ret;
}

Onboarding::MsgCompleteSend MessageOnboardingHandler::Handle(Onboarding::MsgCompleteRecv&& data,
                                                              std::error_condition& errc) {
    (void)data;
    Onboarding::MsgCompleteSend ret{};
    onboarding_service_.CompleteOnboarding();
    errc = util::ErrorEnum::Success;
    return ret;
}

Onboarding::MsgResetDemoSend MessageOnboardingHandler::Handle(Onboarding::MsgResetDemoRecv&& data,
                                                               std::error_condition& errc) {
    (void)data;
    Onboarding::MsgResetDemoSend ret{};
    onboarding_service_.ResetDemo();
    errc = util::ErrorEnum::Success;
    return ret;
}

}  // namespace cosmo
