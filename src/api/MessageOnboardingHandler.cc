/// @file MessageOnboardingHandler.cc
/// @brief Handler implementation for Onboarding Wizard REST API.
#include "api/MessageOnboardingHandler.h"

#include "service/onboarding/IOnboardingService.h"
#include "util/ErrorCode.h"

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

Onboarding::MsgCompleteSend MessageOnboardingHandler::Handle(Onboarding::MsgCompleteRecv&& data,
                                                              std::error_condition& errc) {
    (void)data;
    Onboarding::MsgCompleteSend ret{};
    onboarding_service_.CompleteOnboarding();
    errc = util::ErrorEnum::Success;
    return ret;
}

Onboarding::MsgResetSend MessageOnboardingHandler::Handle(Onboarding::MsgResetRecv&& data,
                                                           std::error_condition& errc) {
    (void)data;
    Onboarding::MsgResetSend ret{};
    onboarding_service_.Reset();
    errc = util::ErrorEnum::Success;
    return ret;
}

}  // namespace cosmo
