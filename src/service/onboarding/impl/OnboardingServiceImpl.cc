/// @file OnboardingServiceImpl.cc
/// @brief Implementation of the Onboarding Wizard lifecycle service.
///        All demo resource paths are resolved internally — never exposed
///        to REST responses or frontend requests.
#include "service/onboarding/impl/OnboardingServiceImpl.h"

#include <filesystem>
#include <mutex>

#include "nlohmann/json.hpp"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/detail/ServiceRegistry.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {

// Internal serialization envelope for onboarding.json
namespace {
struct OnboardingState {
    bool onboarding_completed{false};
    std::string demo_camera_id;
};

void to_json(nlohmann::json& j, const OnboardingState& s) {
    j = nlohmann::json{
        {"onboardingCompleted", s.onboarding_completed},
        {"demoCameraId", s.demo_camera_id},
    };
}

void from_json(const nlohmann::json& j, OnboardingState& s) {
    j.at("onboardingCompleted").get_to(s.onboarding_completed);
    j.at("demoCameraId").get_to(s.demo_camera_id);
}
}  // namespace

// ── Construction ────────────────────────────────────────────────────────

OnboardingServiceImpl::OnboardingServiceImpl() {
    LoadState();
    MLOG_INFO("OnboardingServiceImpl: state loaded, completed={}", onboarding_completed_);
}

// ── Public API ──────────────────────────────────────────────────────────

bool OnboardingServiceImpl::IsOnboardingCompleted() {
    std::lock_guard<std::mutex> lock(mtx_);
    return onboarding_completed_;
}

OnboardingDemoResult OnboardingServiceImpl::StartDemo() {
    std::lock_guard<std::mutex> lock(mtx_);

    // Guard: don't create duplicate demo cameras
    if (!demo_camera_id_.empty()) {
        MLOG_WARN("OnboardingServiceImpl::StartDemo: demo already active, cameraId={}",
                  demo_camera_id_);
        OnboardingDemoResult result;
        result.camera_id      = demo_camera_id_;
        result.camera_name    = kDemoCameraName;
        result.algorithm_code = "";  // Already bound
        return result;
    }

    // 1. Verify the built-in demo video file exists
    if (!cosmo::util::FileExist(kDemoVideoPath)) {
        MLOG_ERRO("OnboardingServiceImpl::StartDemo: demo video not found at internal path");
        throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::FileNotExist,
                                        "Built-in demo video not available");
    }

    // 2. Register a virtual camera pointing to the internal demo video
    cosmo::MsgCameraInfo camera_info;
    camera_info.url          = kDemoVideoPath;  // Internal only — never returned to frontend
    camera_info.channelCode  = kDemoCameraCode;
    camera_info.channelName  = kDemoCameraName;
    camera_info.channelType  = cosmo::MsgCameraType::MsgCameraTypeLocalVideo;

    std::string camera_id;
    auto errc = ServiceRegistry::Instance().Get<ICameraDeviceCrud>().Add(camera_info, camera_id);
    if (errc != cosmo::util::ErrorEnum::Success) {
        MLOG_ERRO("OnboardingServiceImpl::StartDemo: failed to register demo camera, errc={}",
                  static_cast<int>(errc));
        throw cosmo::util::ErrorMessage(errc, "Failed to register demo camera");
    }

    demo_camera_id_ = camera_id;
    SaveState();

    MLOG_INFO("OnboardingServiceImpl::StartDemo: demo camera registered, cameraId={}", camera_id);

    // 3. Return the abstract result (no physical paths exposed)
    OnboardingDemoResult result;
    result.camera_id      = camera_id;
    result.camera_name    = kDemoCameraName;
    result.algorithm_code = "";  // Algorithm binding is done by frontend via Task/SaveOrUpdate
    return result;
}

void OnboardingServiceImpl::CompleteOnboarding() {
    std::lock_guard<std::mutex> lock(mtx_);
    onboarding_completed_ = true;
    SaveState();
    MLOG_INFO("OnboardingServiceImpl::CompleteOnboarding: onboarding marked as completed");
}

void OnboardingServiceImpl::ResetDemo() {
    std::lock_guard<std::mutex> lock(mtx_);

    if (demo_camera_id_.empty()) {
        MLOG_INFO("OnboardingServiceImpl::ResetDemo: no demo camera to clean up");
        return;
    }

    // Delete the demo camera (this also stops and removes associated tasks)
    auto errc = ServiceRegistry::Instance().Get<ICameraDeviceCrud>().Delete(demo_camera_id_);
    if (errc != cosmo::util::ErrorEnum::Success) {
        MLOG_WARN("OnboardingServiceImpl::ResetDemo: failed to delete demo camera {}, errc={}",
                  demo_camera_id_, static_cast<int>(errc));
    } else {
        MLOG_INFO("OnboardingServiceImpl::ResetDemo: demo camera {} deleted", demo_camera_id_);
    }

    demo_camera_id_.clear();
    onboarding_completed_ = false;
    SaveState();
    MLOG_INFO("OnboardingServiceImpl::ResetDemo: onboarding state fully reset");
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
                demo_camera_id_       = state.demo_camera_id;
            } catch (const nlohmann::json::exception& e) {
                MLOG_WARN("OnboardingServiceImpl::LoadState: parse error: {}", e.what());
                onboarding_completed_ = false;
                demo_camera_id_.clear();
            }
        }
    } else {
        // First boot or factory reset — onboarding not completed
        onboarding_completed_ = false;
        demo_camera_id_.clear();
    }
}

void OnboardingServiceImpl::SaveState() {
    OnboardingState state;
    state.onboarding_completed = onboarding_completed_;
    state.demo_camera_id       = demo_camera_id_;
    nlohmann::json j = state;
    auto path         = GetConfigFilePath();
    cosmo::util::WriteFile(path, j.dump(2));
}

}  // namespace cosmo::service
