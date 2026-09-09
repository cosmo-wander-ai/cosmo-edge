#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
// Unit tests for AuthServiceImpl — login, token management, password change.
// Uses a unique temp config directory per test to isolate from device state.

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "service/network/impl/AuthServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "util/CipherUtil.h"
#include "util/StringUtil.h"

namespace fs = std::filesystem;
using namespace cosmo::service;

// Helper: compute upper-case MD5 matching AuthServiceImpl's internal comparison
static std::string Md5Upper(const std::string& input) {
    return cosmo::util::ToUpper(cosmo::util::EncMd5(input));
}

// Helper: create a unique temp config dir so Load()/Save() don't touch device data.
struct IsolatedAuthEnv {
    std::string tmpDir;
    cosmo::test::ScopedPathOverride pathOverride;

    IsolatedAuthEnv()
        : tmpDir("/tmp/cosmo_auth_test_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
          pathOverride(tmpDir, tmpDir) {
        fs::create_directories(tmpDir + "/conf");
    }

    ~IsolatedAuthEnv() {
        std::error_code ec;
        fs::remove_all(tmpDir, ec);
    }
};

TEST_CASE("AuthServiceImpl: Login", "[auth-service]") {
    IsolatedAuthEnv env;
    AuthServiceImpl sut;

    SECTION("Login with default admin/admin succeeds") {
        auto [token, err] = sut.Login("admin", Md5Upper("admin"));
        REQUIRE(err == cosmo::util::ErrorEnum::Success);
        REQUIRE_FALSE(token.empty());
    }

    SECTION("Login with wrong password fails") {
        auto [token, err] = sut.Login("admin", Md5Upper("wrong_pass"));
        REQUIRE(err == cosmo::util::ErrorEnum::LoginFailed);
        REQUIRE(token.empty());
    }

    SECTION("Login with nonexistent user fails") {
        auto [token, err] = sut.Login("unknown_user", Md5Upper("admin"));
        REQUIRE(err == cosmo::util::ErrorEnum::LoginFailed);
        REQUIRE(token.empty());
    }

    SECTION("Multiple logins produce unique tokens") {
        auto [token1, err1] = sut.Login("admin", Md5Upper("admin"));
        auto [token2, err2] = sut.Login("admin", Md5Upper("admin"));
        REQUIRE(err1 == cosmo::util::ErrorEnum::Success);
        REQUIRE(err2 == cosmo::util::ErrorEnum::Success);
        REQUIRE(token1 != token2);
        std::string principal1;
        std::string principal2;
        REQUIRE(sut.ResolvePrincipal(token1, principal1));
        REQUIRE(sut.ResolvePrincipal(token2, principal2));
        REQUIRE(principal1 == "admin");
        REQUIRE(principal2 == principal1);
    }
}

TEST_CASE("AuthServiceImpl: Token validation", "[auth-service]") {
    IsolatedAuthEnv env;
    AuthServiceImpl sut;

    SECTION("Valid token is accepted") {
        auto [token, err] = sut.Login("admin", Md5Upper("admin"));
        REQUIRE(err == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.IsValidToken(token));
    }

    SECTION("Unknown token is rejected") {
        REQUIRE_FALSE(sut.IsValidToken("nonexistent-token"));
        std::string principal = "stale";
        REQUIRE_FALSE(sut.ResolvePrincipal("nonexistent-token", principal));
        REQUIRE(principal.empty());
    }

    SECTION("Empty token is rejected") {
        REQUIRE_FALSE(sut.IsValidToken(""));
    }
}

TEST_CASE("AuthServiceImpl: Sessions are process local", "[auth-service]") {
    IsolatedAuthEnv env;
    AuthServiceImpl first_instance;

    auto [token, login_error] = first_instance.Login("admin", Md5Upper("admin"));
    REQUIRE(login_error == cosmo::util::ErrorEnum::Success);
    REQUIRE(first_instance.IsValidToken(token));

    std::ifstream auth_file(env.tmpDir + "/conf/auth.json");
    REQUIRE(auth_file.is_open());
    nlohmann::json persisted;
    auth_file >> persisted;
    REQUIRE(persisted.contains("userPasswd"));
    REQUIRE_FALSE(persisted.contains("token"));

    AuthServiceImpl restarted_instance;
    REQUIRE_FALSE(restarted_instance.IsValidToken(token));
}

TEST_CASE("AuthServiceImpl: Legacy persisted sessions are discarded", "[auth-service]") {
    IsolatedAuthEnv env;
    const auto auth_path = env.tmpDir + "/conf/auth.json";
    {
        std::ofstream auth_file(auth_path);
        REQUIRE(auth_file.is_open());
        auth_file << nlohmann::json{
            {"userPasswd", {{"admin", Md5Upper("admin")}}},
            {"token", nlohmann::json::array({{{"user", "admin"}, {"token", "legacy-session"}}})}};
    }

    AuthServiceImpl sut;
    REQUIRE_FALSE(sut.IsValidToken("legacy-session"));

    std::ifstream auth_file(auth_path);
    nlohmann::json persisted;
    auth_file >> persisted;
    REQUIRE_FALSE(persisted.contains("token"));
}

TEST_CASE("AuthServiceImpl: Change password", "[auth-service]") {
    IsolatedAuthEnv env;
    AuthServiceImpl sut;

    auto defaultPwdMd5 = Md5Upper("admin");
    auto newPwdMd5     = Md5Upper("new_password_123");

    SECTION("Change password with valid token succeeds") {
        auto [token, loginErr] = sut.Login("admin", defaultPwdMd5);
        REQUIRE(loginErr == cosmo::util::ErrorEnum::Success);

        auto changeErr = sut.ChangePasswd(token, defaultPwdMd5, newPwdMd5);
        REQUIRE(changeErr == cosmo::util::ErrorEnum::Success);
        REQUIRE_FALSE(sut.IsValidToken(token));

        // Old password should no longer work
        auto [token2, err2] = sut.Login("admin", defaultPwdMd5);
        REQUIRE(err2 == cosmo::util::ErrorEnum::LoginFailed);

        // New password should work
        auto [token3, err3] = sut.Login("admin", newPwdMd5);
        REQUIRE(err3 == cosmo::util::ErrorEnum::Success);
    }

    SECTION("Change password with invalid token fails") {
        auto changeErr = sut.ChangePasswd("bad-token", defaultPwdMd5, newPwdMd5);
        REQUIRE(changeErr == cosmo::util::ErrorEnum::NotLogin);
    }

    SECTION("Change password with wrong old password fails") {
        auto [token, loginErr] = sut.Login("admin", defaultPwdMd5);
        REQUIRE(loginErr == cosmo::util::ErrorEnum::Success);

        auto changeErr = sut.ChangePasswd(token, Md5Upper("wrong_old"), newPwdMd5);
        REQUIRE(changeErr == cosmo::util::ErrorEnum::OldPasswdWrong);
    }
}

TEST_CASE("AuthServiceImpl: Password change revokes every session for that user", "[auth-service]") {
    IsolatedAuthEnv env;
    const auto operator_password = Md5Upper("operator-password");
    {
        std::ofstream auth_file(env.tmpDir + "/conf/auth.json");
        REQUIRE(auth_file.is_open());
        auth_file << nlohmann::json{
            {"userPasswd", {{"admin", Md5Upper("admin")}, {"operator", operator_password}}}};
    }

    AuthServiceImpl sut;
    auto [admin_token_a, admin_error_a]   = sut.Login("admin", Md5Upper("admin"));
    auto [admin_token_b, admin_error_b]   = sut.Login("admin", Md5Upper("admin"));
    auto [operator_token, operator_error] = sut.Login("operator", operator_password);
    REQUIRE(admin_error_a == cosmo::util::ErrorEnum::Success);
    REQUIRE(admin_error_b == cosmo::util::ErrorEnum::Success);
    REQUIRE(operator_error == cosmo::util::ErrorEnum::Success);

    REQUIRE(sut.ChangePasswd(admin_token_a, Md5Upper("admin"), Md5Upper("new-admin-password")) ==
            cosmo::util::ErrorEnum::Success);
    REQUIRE_FALSE(sut.IsValidToken(admin_token_a));
    REQUIRE_FALSE(sut.IsValidToken(admin_token_b));
    REQUIRE(sut.IsValidToken(operator_token));
}
