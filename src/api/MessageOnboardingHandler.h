/// @file MessageOnboardingHandler.h
/// @brief Handler for Onboarding Wizard REST API messages.
#pragma once

#include <system_error>

#include "service/onboarding/dto/OnboardingDto.h"

namespace cosmo::service {
class IOnboardingService;
}  // namespace cosmo::service

namespace cosmo {

class MessageOnboardingHandler {
public:
    explicit MessageOnboardingHandler(service::IOnboardingService& onboarding_service);

    Onboarding::MsgStatusSend Handle(Onboarding::MsgStatusRecv&& data, std::error_condition& errc);
    Onboarding::MsgCompleteSend Handle(Onboarding::MsgCompleteRecv&& data,
                                        std::error_condition& errc);
    Onboarding::MsgResetSend Handle(Onboarding::MsgResetRecv&& data, std::error_condition& errc);

private:
    service::IOnboardingService& onboarding_service_;
};

}  // namespace cosmo
