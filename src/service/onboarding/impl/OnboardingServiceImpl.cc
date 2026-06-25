/// @file OnboardingServiceImpl.cc
/// @brief Implementation of the Onboarding Wizard lifecycle service.
#include "service/onboarding/impl/OnboardingServiceImpl.h"

#include <filesystem>
#include <mutex>

#include "nlohmann/json.hpp"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {

namespace {
struct OnboardingState {
    bool onboarding_completed{false};
};

void to_json(nlohmann::json& j, const OnboardingState& s) {
    j = nlohmann::json{{"onboardingCompleted", s.onboarding_completed}};
}

void from_json(const nlohmann::json& j, OnboardingState& s) {
    j.at("onboardingCompleted").get_to(s.onboarding_completed);
}
}  // namespace

// ── Construction ────────────────────────────────────────────────────────

OnboardingServiceImpl::OnboardingServiceImpl() {
    LoadState();
    LOG_INFO("OnboardingServiceImpl: state loaded, completed={}", onboarding_completed_);
}

// ── Public API ──────────────────────────────────────────────────────────

bool OnboardingServiceImpl::IsOnboardingCompleted() {
    std::lock_guard<std::mutex> lock(mtx_);
    return onboarding_completed_;
}

void OnboardingServiceImpl::CompleteOnboarding() {
    std::lock_guard<std::mutex> lock(mtx_);
    onboarding_completed_ = true;
    SaveState();
    LOG_INFO("OnboardingServiceImpl::CompleteOnboarding: onboarding marked as completed");
}

void OnboardingServiceImpl::Reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    onboarding_completed_ = false;
    SaveState();
    LOG_INFO("OnboardingServiceImpl::Reset: onboarding state reset");
}

// ── Private Persistence ─────────────────────────────────────────────────

std::string OnboardingServiceImpl::GetConfigFilePath() {
    return (std::filesystem::path(cosmo::path::GetCfgPath()) / kConfigFileName).string();
}

void OnboardingServiceImpl::LoadState() {
    OnboardingState state;
    auto path = GetConfigFilePath();
    if (cosmo::util::FileExist(path)) {
        auto content = cosmo::util::ReadFile(path);
        if (!content.empty()) {
            try {
                auto j = nlohmann::json::parse(content);
                j.get_to(state);
                onboarding_completed_ = state.onboarding_completed;
            } catch (const nlohmann::json::exception& e) {
                LOG_WARN("OnboardingServiceImpl::LoadState: parse error: {}", e.what());
                onboarding_completed_ = false;
            }
        }
    } else {
        onboarding_completed_ = false;
    }
}

void OnboardingServiceImpl::SaveState() {
    OnboardingState state;
    state.onboarding_completed = onboarding_completed_;
    nlohmann::json j = state;
    auto path         = GetConfigFilePath();
    cosmo::util::WriteFile(path, j.dump(2));
}

}  // namespace cosmo::service
