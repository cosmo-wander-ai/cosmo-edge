/// @file OnboardingServiceImpl.h
/// @brief Implementation of the Onboarding Wizard lifecycle service.
#pragma once

#include <mutex>
#include <string>

#include "service/onboarding/IOnboardingService.h"

namespace cosmo::service {

class OnboardingServiceImpl : public IOnboardingService {
public:
    OnboardingServiceImpl();
    ~OnboardingServiceImpl() override = default;

    // ---- IOnboardingService ----
    bool IsOnboardingCompleted() override;
    void CompleteOnboarding() override;
    void Reset() override;

private:
    /// Load persisted onboarding state from disk.
    void LoadState();
    /// Persist current onboarding state to disk.
    void SaveState();
    /// Build the configuration file path for onboarding state.
    static std::string GetConfigFilePath();

    mutable std::mutex mtx_;
    bool onboarding_completed_{false};

    static constexpr const char* kConfigFileName = "onboarding.json";
};

}  // namespace cosmo::service
