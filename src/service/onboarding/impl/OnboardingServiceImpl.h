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
    OnboardingDemoResult StartDemo() override;
    void CompleteOnboarding() override;
    void ResetDemo() override;

private:
    /// Load persisted onboarding state from disk.
    void LoadState();
    /// Persist current onboarding state to disk.
    void SaveState();
    /// Build the configuration file path for onboarding state.
    static std::string GetConfigFilePath();

    mutable std::mutex mtx_;
    bool onboarding_completed_{false};
    std::string demo_camera_id_;  ///< Tracks the demo camera ID for cleanup.

    // Well-known constants (internal only — never exposed to REST responses)
    static constexpr const char* kDemoVideoPath =
        "/data/cwaiuserdata/resource/demo/demo.mp4";
    static constexpr const char* kDemoCameraName = "演示相机";
    static constexpr const char* kDemoCameraCode = "ONBOARDING_DEMO_001";
    static constexpr const char* kConfigFileName = "onboarding.json";
};

}  // namespace cosmo::service
