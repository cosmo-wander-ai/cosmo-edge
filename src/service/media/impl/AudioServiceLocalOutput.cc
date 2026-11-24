#include <algorithm>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>

#include "service/media/impl/AudioServiceImpl.h"
#include "service/media/impl/LocalAudioPlayer.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"

namespace cosmo::service {
namespace {
    struct LocalAudioSettings {
        bool enabled{false};
        std::string fileId;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(LocalAudioSettings, enabled, fileId)
    };

    std::string LocalAudioConfigPath() {
        return path::GetCfgPath() + "/localAudioOut.json";
    }
}  // namespace

// Called during construction or with the library lock held.
bool AudioServiceImpl::ResolveLocalAudioSource(const std::string& file_id, std::string& source,
                                               std::string& reason) const {
    namespace fs     = std::filesystem;
    const auto entry = std::find_if(audio_info_list_.begin(), audio_info_list_.end(),
                                    [&](const AlarmAudioInfo& audio) { return audio.id == file_id; });
    if (entry == audio_info_list_.end()) {
        reason = "fileNotFound";
        return false;
    }
    const bool preset  = file_id == default_audio_id_;
    fs::path candidate = default_audio_file_name_;
    if (!preset) {
        if (!path::IsSafePathComponent(entry->fileName)) {
            reason = "invalidAudioFile";
            return false;
        }
        candidate      = fs::path(audio_file_path_) / entry->fileName;
        const auto ext = util::ToLower(candidate.extension().string());
        if (ext != ".mp3" && ext != ".wav") {
            reason = "invalidAudioFile";
            return false;
        }
    }
    const auto root = preset ? path::GetAppBaseDir() : path::GetBaseDir();
    if (!path::ResolveExistingPathWithinRoot(root, candidate.string(), path::PathEntryType::kRegularFile,
                                             source)) {
        std::error_code ec;
        reason = fs::symlink_status(candidate, ec).type() == fs::file_type::not_found ? "fileNotFound"
                                                                                      : "invalidAudioFile";
        return false;
    }
    return true;
}

void AudioServiceImpl::LoadLocalAudioOut() {
    const auto config_path = LocalAudioConfigPath();
    std::error_code ec;
    if (std::filesystem::symlink_status(config_path, ec).type() == std::filesystem::file_type::not_found) {
        return;
    }
    std::string resolved;
    LocalAudioSettings settings;
    if (!path::ResolveExistingPathWithinRoot(path::GetCfgPath(), config_path,
                                             path::PathEntryType::kRegularFile, resolved) ||
        !util::LoadStructFromJsonFile(resolved, settings) || !path::IsSafePathComponent(settings.fileId)) {
        local_audio_error_ = "configInvalid";
        LOG_WARN("Invalid local Audio Out configuration");
        return;
    }
    local_audio_enabled_ = settings.enabled;
    local_audio_file_id_ = std::move(settings.fileId);
    if (local_audio_enabled_) {
        std::string source;
        if (ResolveLocalAudioSource(local_audio_file_id_, source, local_audio_error_)) {
            local_audio_player_->SetClip(
                local_audio_player_->Prepare(local_audio_file_id_, source, local_audio_error_));
        }
        if (!local_audio_error_.empty()) {
            LOG_WARN("Local Audio Out could not restore its recording: {}", local_audio_error_);
        }
    }
}

LocalAudioOutState AudioServiceImpl::QueryLocalAudioOut() {
    std::shared_lock lock(audio_mtx_);
    auto reason          = local_audio_player_->UnavailableReason();
    const bool available = reason.empty();
    return {available, available ? local_audio_error_ : std::move(reason), local_audio_enabled_,
            local_audio_file_id_};
}

bool AudioServiceImpl::SetLocalAudioOut(bool enabled, const std::string& file_id, std::string& reason) {
    reason.clear();
    if (!path::IsSafePathComponent(file_id)) {
        reason = "invalidAudioFile";
        return false;
    }
    std::unique_lock lock(audio_mtx_);
    LocalAudioPlayer::ClipPtr clip;
    if (enabled || file_id != local_audio_file_id_) {
        std::string source;
        if (!ResolveLocalAudioSource(file_id, source, reason)) {
            return false;
        }
        if (enabled) {
            reason = local_audio_player_->UnavailableReason();
            if (!reason.empty()) {
                return false;
            }
        }
        clip = local_audio_player_->Prepare(file_id, source, reason);
        if (!clip) {
            return false;
        }
    }
    LocalAudioSettings settings{enabled, file_id};
    if (!util::SaveStructToJsonFile(LocalAudioConfigPath(), settings)) {
        reason = "configWriteFailed";
        return false;
    }
    local_audio_enabled_ = enabled;
    local_audio_file_id_ = std::move(settings.fileId);
    local_audio_error_.clear();
    local_audio_player_->SetClip(enabled ? std::move(clip) : nullptr);
    return true;
}

void AudioServiceImpl::QueueLocalAlarm() {
    local_audio_player_->Enqueue();
}

void AudioServiceImpl::StopLocalAudioOut() {
    local_audio_player_->Stop();
}

}  // namespace cosmo::service
