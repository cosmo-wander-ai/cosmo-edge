#include "catch_amalgamated.hpp"
// Unit tests for MessageAuthHandler — verifies Handler-layer request dispatch.
// Uses MockAuthService via DI constructor injection.

#include "api/MessageAuthHandler.h"
#include "mock/MockAuthService.h"
#include "mock/MockServiceRegistry.h"
#include "util/ErrorCode.h"

using namespace cosmo;

TEST_CASE("MessageAuthHandler: Login success", "[AuthHandler]") {
    test::MockServiceRegistry mocks;
    MessageAuthHandler handler(mocks.authSvc);

    REQUIRE_CALL(mocks.authSvc, Login("admin", "pass123"))
        .RETURN(std::make_pair(std::string("token_abc"), util::ErrorEnum::Success));

    Auth::MsgDoLoginRecv req{};
    req.account = "admin";
    req.pwd     = "pass123";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
    REQUIRE(rsp.resData.mtk == "token_abc");
    REQUIRE(rsp.resData.accountName == "admin");
}

TEST_CASE("MessageAuthHandler: Login failure", "[AuthHandler]") {
    test::MockServiceRegistry mocks;
    MessageAuthHandler handler(mocks.authSvc);

    REQUIRE_CALL(mocks.authSvc, Login("admin", "wrongpwd"))
        .RETURN(std::make_pair(std::string(""), util::ErrorEnum::LoginFailed));

    Auth::MsgDoLoginRecv req{};
    req.account = "admin";
    req.pwd     = "wrongpwd";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::LoginFailed);
    REQUIRE(rsp.resData.mtk.empty());
}

TEST_CASE("MessageAuthHandler: Login frequency limit", "[AuthHandler]") {
    test::MockServiceRegistry mocks;
    MessageAuthHandler handler(mocks.authSvc);

    REQUIRE_CALL(mocks.authSvc, Login(trompeloeil::_, trompeloeil::_))
        .RETURN(std::make_pair(std::string{}, util::ErrorEnum::LoginFrequence));

    Auth::MsgDoLoginRecv req{};
    req.account = "admin";
    req.pwd     = "pass123";
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::LoginFrequence);
    REQUIRE(rsp.resData.mtk.empty());
}

TEST_CASE("MessageAuthHandler: ChangePassword success", "[AuthHandler]") {
    test::MockServiceRegistry mocks;
    MessageAuthHandler handler(mocks.authSvc);

    REQUIRE_CALL(mocks.authSvc, ChangePasswd("tok", "old", "new")).RETURN(util::ErrorEnum::Success);

    Auth::MsgModifyPasswordRecv req{};
    req.mtk       = "tok";
    req.passwdOld = "old";
    req.passwdNew = "new";
    std::error_condition errc;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageAuthHandler: ChangePassword not logged in", "[AuthHandler]") {
    test::MockServiceRegistry mocks;
    MessageAuthHandler handler(mocks.authSvc);

    REQUIRE_CALL(mocks.authSvc, ChangePasswd("bad-tok", "old", "new")).RETURN(util::ErrorEnum::NotLogin);

    Auth::MsgModifyPasswordRecv req{};
    req.mtk       = "bad-tok";
    req.passwdOld = "old";
    req.passwdNew = "new";
    std::error_condition errc;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::NotLogin);
}

TEST_CASE("MessageAuthHandler: ChangePassword wrong old", "[AuthHandler]") {
    test::MockServiceRegistry mocks;
    MessageAuthHandler handler(mocks.authSvc);

    REQUIRE_CALL(mocks.authSvc, ChangePasswd("tok", "wrong", "new")).RETURN(util::ErrorEnum::OldPasswdWrong);

    Auth::MsgModifyPasswordRecv req{};
    req.mtk       = "tok";
    req.passwdOld = "wrong";
    req.passwdNew = "new";
    std::error_condition errc;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::OldPasswdWrong);
}
