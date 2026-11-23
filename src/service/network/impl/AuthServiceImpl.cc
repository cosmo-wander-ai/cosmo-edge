// AuthService — authentication logic (login, token, password).
// Migrated from the former AuthorManager singleton.

#include "service/network/impl/AuthServiceImpl.h"

#include <filesystem>

#include "service/detail/ServiceRegistry.h"
#include "util/CipherUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/UuidUtil.h"

namespace chrono = std::chrono;

namespace cosmo::service {

// ── Construction ──

AuthServiceImpl::AuthServiceImpl() {
    Load();

    if (user_passwd_.empty()) {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        user_passwd_["admin"] = kDefaultAdminPasswordHash;
        Save();
    }
}

// ── Persistence ──

void AuthServiceImpl::Load() {
    PersistData data;
    auto cfg_path = (std::filesystem::path(cosmo::path::GetCfgPath()) / config_name_).string();
    if (!util::LoadStructFromJsonFile(cfg_path, data)) {
        LOG_WARN("Failed to load auth config from {}", cfg_path);
    }

    std::lock_guard<std::shared_mutex> lock(mtx_);
    user_passwd_ = std::move(data.userPasswd);
    token_user_.clear();
    token_time_.clear();

    // Rewrite legacy files without their persisted session tokens. Sessions are
    // process-local credentials and must never survive a restart.
    if (data.had_legacy_token) {
        Save();
    }
}

bool AuthServiceImpl::Save() {
    PersistData data;
    data.userPasswd = user_passwd_;

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / config_name_).string();
    if (!cosmo::util::SaveStructToJsonFile(path, data)) {
        LOG_WARN("Failed to save auth config to {}", path);
        return false;
    }
    return true;
}

// ── Token helpers (caller must hold mtx_) ──

std::string AuthServiceImpl::CreateToken(const std::string& userName) {
    auto token = cosmo::util::GenerateUUID();
    auto now   = Clock::now();
    token_user_.insert({token, {userName, now}});
    token_time_.emplace(now, token);
    return token;
}

bool AuthServiceImpl::PurgeExpiredTokens(TimePoint now) {
    auto cutoff = now - chrono::seconds(kTokenExpireSec);
    auto it_end = token_time_.lower_bound(cutoff);
    if (it_end == token_time_.begin()) {
        return false;
    }
    for (auto it = token_time_.begin(); it != it_end;) {
        token_user_.erase(it->second);
        it = token_time_.erase(it);
    }
    return true;
}

void AuthServiceImpl::RevokeUserTokens(const std::string& user_name) {
    for (auto token_it = token_user_.begin(); token_it != token_user_.end();) {
        if (token_it->second.userName != user_name) {
            ++token_it;
            continue;
        }

        const auto token = token_it->first;
        for (auto time_it = token_time_.begin(); time_it != token_time_.end();) {
            if (time_it->second == token) {
                time_it = token_time_.erase(time_it);
            } else {
                ++time_it;
            }
        }
        token_it = token_user_.erase(token_it);
    }
}

std::string AuthServiceImpl::FindUserByToken(const std::string& token) const {
    auto it = token_user_.find(token);
    return (it != token_user_.end()) ? it->second.userName : std::string{};
}

// ── IAuthService ──

std::pair<std::string, cosmo::util::ErrorEnum> AuthServiceImpl::Login(const std::string& user,
                                                                      const std::string& passwdMd5) {
    std::lock_guard<std::shared_mutex> lock(mtx_);

    auto it = user_passwd_.find(user);
    if (it == user_passwd_.end() || it->second != cosmo::util::ToUpper(passwdMd5)) {
        return {{}, cosmo::util::ErrorEnum::LoginFailed};
    }

    if (token_time_.size() >= kMaxTokenCount) {
        PurgeExpiredTokens(Clock::now());
        if (token_time_.size() >= kMaxTokenCount) {
            return {{}, cosmo::util::ErrorEnum::LoginFrequence};
        }
    }

    auto token = CreateToken(user);
    return {token, cosmo::util::ErrorEnum::Success};
}

cosmo::util::ErrorEnum AuthServiceImpl::ChangePasswd(const std::string& token,
                                                     const std::string& passwdMd5Old,
                                                     const std::string& passwdMd5New) {
    std::lock_guard<std::shared_mutex> lock(mtx_);

    auto user = FindUserByToken(token);
    if (user.empty()) {
        return cosmo::util::ErrorEnum::NotLogin;
    }

    auto it = user_passwd_.find(user);
    if (it == user_passwd_.end() || it->second != cosmo::util::ToUpper(passwdMd5Old)) {
        return cosmo::util::ErrorEnum::OldPasswdWrong;
    }

    const auto old_password = it->second;
    it->second              = cosmo::util::ToUpper(passwdMd5New);
    if (!Save()) {
        it->second = old_password;
        return cosmo::util::ErrorEnum::FileOpenFailed;
    }

    RevokeUserTokens(user);
    return cosmo::util::ErrorEnum::Success;
}

bool AuthServiceImpl::IsValidToken(const std::string& token) {
    std::string principal;
    return ResolvePrincipal(token, principal);
}

bool AuthServiceImpl::ResolvePrincipal(const std::string& token, std::string& principal) {
    principal.clear();
    auto now = Clock::now();
    std::lock_guard<std::shared_mutex> lock(mtx_);

    PurgeExpiredTokens(now);

    auto it = token_user_.find(token);
    if (it == token_user_.end()) {
        return false;
    }

    // Refresh token time — use precise iterator removal to avoid erasing other tokens at the same time
    auto [range_begin, range_end] = token_time_.equal_range(it->second.createdAt);
    for (auto time_it = range_begin; time_it != range_end; ++time_it) {
        if (time_it->second == token) {
            token_time_.erase(time_it);
            break;
        }
    }
    token_time_.emplace(now, token);
    TokenEntry& entry = it->second;
    entry.createdAt   = now;
    principal         = entry.userName;
    return true;
}

bool AuthServiceImpl::IsDefaultPassword() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& [user, hash] : user_passwd_) {
        if (hash == kDefaultAdminPasswordHash) {
            return true;
        }
    }
    return false;
}

}  // namespace cosmo::service

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::service {
void to_json(nlohmann::json& j, const AuthServiceImpl::PersistData& v) {
    j["userPasswd"] = v.userPasswd;
}

void from_json(const nlohmann::json& j, AuthServiceImpl::PersistData& v) {
    v.had_legacy_token = j.contains("token");
    if (j.contains("userPasswd") && !j["userPasswd"].is_null())
        j.at("userPasswd").get_to(v.userPasswd);
}

}  // namespace cosmo::service
