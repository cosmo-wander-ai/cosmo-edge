/// @file IAuthService.h
/// @brief Authentication service interface — user login, token validation,
///        and password management.
#pragma once

#include <string>
#include <utility>

#include "util/ErrorCode.h"

namespace cosmo::service {

/// Manages user authentication: login with credentials, session token
/// validation, and password changes.
class IAuthService {
public:
    virtual ~IAuthService() = default;

    /// Authenticate a user and issue a session token.
    /// @param user      Username.
    /// @param passwdMd5 MD5 hash of the password.
    /// @return Pair of (session token, error code). Token is empty on failure.
    virtual std::pair<std::string, cosmo::util::ErrorEnum> Login(const std::string& user,
                                                                 const std::string& passwdMd5) = 0;

    /// Change a user's password (requires current session token).
    /// @param token         Active session token.
    /// @param passwdMd5Old  MD5 hash of the current password.
    /// @param passwdMd5New  MD5 hash of the new password.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ChangePasswd(const std::string& token, const std::string& passwdMd5Old,
                                                const std::string& passwdMd5New) = 0;

    /// Validate whether a session token is still active.
    /// @param token Session token to validate.
    /// @return true if the token is valid and not expired.
    virtual bool IsValidToken(const std::string& token) = 0;

    /// Revoke only this session; return false if it no longer exists.
    virtual bool RevokeToken(const std::string& token) = 0;

    /// Validate an active session token and resolve it to a stable account
    /// identity. Resource ownership and quotas must use this principal rather
    /// than the replaceable bearer token.
    virtual bool ResolvePrincipal(const std::string& token, std::string& principal) {
        principal.clear();
        if (!IsValidToken(token)) {
            return false;
        }
        // Compatibility fallback for test doubles and alternate implementations.
        // Production AuthServiceImpl overrides this with the account username.
        principal = token;
        return true;
    }

    /// Check whether any user account still uses the factory-default password.
    /// The login response includes a `passwordChangeRequired` flag derived from
    /// this check, prompting the UI to force a password change on first login.
    /// @return true if at least one account has the default password.
    virtual bool IsDefaultPassword() const = 0;
};

}  // namespace cosmo::service
