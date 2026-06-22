/// @file IAuthService.h
/// @brief Authentication service interface — user login, token validation,
///        and password management.
#pragma once

#include <string>
#include <utility>

#include "service/detail/ServiceRegistry.h"
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
};

}  // namespace cosmo::service
