/// @file IOnboardingService.h
/// @brief Service interface for the Onboarding Wizard lifecycle.
///        Provides status query and completion persistence.
#pragma once

#include "util/ErrorCode.h"

namespace cosmo::service {

/// Onboarding Wizard lifecycle service.
class IOnboardingService {
public:
    virtual ~IOnboardingService() = default;

    /// Check whether the onboarding wizard has been completed.
    /// For fresh / factory-reset devices this returns false.
    virtual bool IsOnboardingCompleted() = 0;

    /// Mark onboarding as completed and persist the state so it survives
    /// device reboots.
    virtual void CompleteOnboarding() = 0;

    /// Reset onboarding state so the wizard runs again on next login.
    virtual void Reset() = 0;
};

}  // namespace cosmo::service
