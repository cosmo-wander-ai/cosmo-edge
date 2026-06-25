#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
// Unit tests for AuthServiceImpl — login, token management, password change.
// Uses a unique temp config directory per test to isolate from device state.

#include <filesystem>

#include "mock/MockServiceRegistry.h"
#include "service/network/impl/AuthServiceImpl.h"
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
    cosmo::test::MockServiceRegistry mocks;
    std::string tmpDir;

    IsolatedAuthEnv() {
        tmpDir = "/tmp/cosmo_auth_test_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        // Override paths so AuthServiceImpl reads/writes to our temp dir
        cosmo::path::OverrideRootPathForTest(tmpDir, tmpDir);
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
    }

    SECTION("Empty token is rejected") {
        REQUIRE_FALSE(sut.IsValidToken(""));
    }
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
