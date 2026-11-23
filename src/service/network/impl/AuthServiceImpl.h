// AuthService — owns authentication state directly (user/password, tokens).
// Persistence via a lightweight AuthPersistData struct (nlohmann/json).

#pragma once

#include <chrono>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <shared_mutex>
#include <string>

#include "service/network/IAuthService.h"

namespace cosmo::service {

class AuthServiceImpl : public IAuthService {
public:
    AuthServiceImpl();
    ~AuthServiceImpl() override = default;

    // ── IAuthService ──
    std::pair<std::string, cosmo::util::ErrorEnum> Login(const std::string& user,
                                                         const std::string& passwdMd5) override;
    cosmo::util::ErrorEnum ChangePasswd(const std::string& token, const std::string& passwdMd5Old,
                                        const std::string& passwdMd5New) override;
    bool IsValidToken(const std::string& token) override;
    bool RevokeToken(const std::string& token) override;
    bool ResolvePrincipal(const std::string& token, std::string& principal) override;
    bool IsDefaultPassword() const override;

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct TokenEntry {
        std::string userName;
        TimePoint createdAt;
    };

public:
    // ── Persistent data structures (public for JSON serialization friend access) ──
    struct PersistData {
        std::map<std::string, std::string> userPasswd;
        bool had_legacy_token{false};

        friend void to_json(nlohmann::json& j, const PersistData& v);
        friend void from_json(const nlohmann::json& j, PersistData& v);
    };

private:
    // ── Internal methods (caller must hold mtx_) ──
    std::string CreateToken(const std::string& userName);
    bool PurgeExpiredTokens(TimePoint now);
    void RevokeUserTokens(const std::string& user_name);
    std::string FindUserByToken(const std::string& token) const;

    void Load();
    bool Save();

    static constexpr int kTokenExpireSec   = 3600;
    static constexpr size_t kMaxTokenCount = 1000;

    // MD5("admin") upper-cased — the factory-default password hash.
    // Used by IsDefaultPassword() to detect unchanged credentials.
    static constexpr const char* kDefaultAdminPasswordHash = "21232F297A57A5A743894A0E4A801FC3";

    mutable std::shared_mutex mtx_;
    std::string config_name_{"auth.json"};
    std::map<std::string, std::string> user_passwd_;    // user → password(MD5, upper)
    std::map<std::string, TokenEntry> token_user_;      // token → entry
    std::multimap<TimePoint, std::string> token_time_;  // time → token (for expiry scan)
};

}  // namespace cosmo::service
