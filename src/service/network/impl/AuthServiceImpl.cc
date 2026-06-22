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
        user_passwd_["admin"] = cosmo::util::ToUpper(cosmo::util::EncMd5("admin"));
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

    auto now = Clock::now();
    for (auto& tp : data.token) {
        token_user_.insert({tp.token, {tp.user, now}});
        token_time_.emplace(now, tp.token);
    }
}

void AuthServiceImpl::Save() {
    PersistData data;
    data.userPasswd = user_passwd_;
    data.token.reserve(token_user_.size());
    for (auto& [tok, entry] : token_user_) {
        if (!tok.empty() && !entry.userName.empty()) {
            data.token.push_back({entry.userName, tok});
        }
    }

    auto path = (std::filesystem::path(cosmo::path::GetCfgPath()) / config_name_).string();
    if (!cosmo::util::SaveStructToJsonFile(path, data)) {
        LOG_WARN("Failed to save auth config to {}", path);
    }
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
            Save();
            return {{}, cosmo::util::ErrorEnum::LoginFrequence};
        }
    }

    auto token = CreateToken(user);
    Save();
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

    it->second = cosmo::util::ToUpper(passwdMd5New);
    Save();
    return cosmo::util::ErrorEnum::Success;
}

bool AuthServiceImpl::IsValidToken(const std::string& token) {
    auto now = Clock::now();
    std::lock_guard<std::shared_mutex> lock(mtx_);

    if (PurgeExpiredTokens(now)) {
        Save();
    }

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
    it->second.createdAt = now;
    return true;
}

}  // namespace cosmo::service

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::service {
void from_json(const nlohmann::json& j, AuthServiceImpl::TokenPair& v) {
    if (j.contains("user") && !j["user"].is_null())
        j.at("user").get_to(v.user);
    if (j.contains("token") && !j["token"].is_null())
        j.at("token").get_to(v.token);
}

void to_json(nlohmann::json& j, const AuthServiceImpl::TokenPair& v) {
    j["user"]  = v.user;
    j["token"] = v.token;
}

void to_json(nlohmann::json& j, const AuthServiceImpl::PersistData& v) {
    j["token"]      = v.token;
    j["userPasswd"] = v.userPasswd;
}

void from_json(const nlohmann::json& j, AuthServiceImpl::PersistData& v) {
    if (j.contains("token") && !j["token"].is_null())
        j.at("token").get_to(v.token);
    if (j.contains("userPasswd") && !j["userPasswd"].is_null())
        j.at("userPasswd").get_to(v.userPasswd);
}

}  // namespace cosmo::service
