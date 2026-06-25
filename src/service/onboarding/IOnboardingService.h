/// @file IOnboardingService.h
/// @brief Service interface for the Onboarding Wizard lifecycle.
///        Provides demo sandbox construction, status persistence,
///        and resource cleanup — all behind an abstract API with
///        no physical path exposure.
#pragma once

#include <string>

#include "util/ErrorCode.h"

namespace cosmo::service {

/// Result of a successful demo sandbox initialization.
struct OnboardingDemoResult {
    std::string camera_id;       ///< Created demo camera channel ID.
    std::string camera_name;     ///< Human-readable name (e.g. "演示相机").
    std::string algorithm_code;  ///< Bound algorithm code (e.g. safety helmet detection).
};

/// Onboarding Wizard lifecycle service.
class IOnboardingService {
public:
    virtual ~IOnboardingService() = default;

    /// Check whether the onboarding wizard has been completed.
    /// For fresh / factory-reset devices this returns false.
    virtual bool IsOnboardingCompleted() = 0;

    /// Create the demo sandbox: register a virtual camera pointing to the
    /// built-in demo video, bind the default safety-helmet-detection algorithm,
    /// and start the inference task.
    /// @return OnboardingDemoResult on success.
    /// @throws util::ErrorMessage on failure.
    virtual OnboardingDemoResult StartDemo() = 0;

    /// Mark onboarding as completed and persist the state so it survives
    /// device reboots.
    virtual void CompleteOnboarding() = 0;

    /// Tear down all resources created by StartDemo() — delete the demo
    /// camera, stop the inference task, and reclaim memory.
    virtual void ResetDemo() = 0;
};

}  // namespace cosmo::service
