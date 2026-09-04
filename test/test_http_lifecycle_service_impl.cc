#include <stdexcept>

#include "catch_amalgamated.hpp"
/*
 * test_http_lifecycle_service_impl.cc — HttpLifecycleServiceImpl unit tests
 *
 * Strategy: Test construction, and Start/Stop idempotency.
 * Full HTTP server tests are tagged [.network].
 */
#include "service/network/impl/HttpLifecycleServiceImpl.h"
#include "util/IRequestDispatcher.h"

using namespace cosmo::service;

namespace {

class StubDispatcher : public cosmo::IRequestDispatcher {
public:
    bool SupportsRoute(const std::string& /*uri*/) override {
        return false;
    }
    cosmo::RequestAdmission InspectRequest(cosmo::RequestDispatchContext& /*context*/,
                                           bool /*require_known_route*/) override {
        return cosmo::RequestAdmission::kRouteNotFound;
    }
    bool DispatchRequest(const cosmo::RequestDispatchContext& /*context*/, const std::string& /*body*/,
                         std::string& /*response*/) override {
        return false;
    }
};

}  // namespace

TEST_CASE("HttpLifecycleServiceImpl: construction and destruction", "[http-lifecycle]") {
    REQUIRE_NOTHROW(
        []() { HttpLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); }); }());
}

TEST_CASE("HttpLifecycleServiceImpl: StopHttpServer without Init is safe", "[http-lifecycle]") {
    HttpLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.RequestHttpStop());
    REQUIRE_NOTHROW(sut.StopHttpServer());
}

TEST_CASE("HttpLifecycleServiceImpl: double StopHttpServer is safe", "[http-lifecycle]") {
    HttpLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.StopHttpServer());
    REQUIRE_NOTHROW(sut.StopHttpServer());
}

TEST_CASE("HttpLifecycleServiceImpl: initialization failure is reported", "[http-lifecycle]") {
    HttpLifecycleServiceImpl sut([]() { return std::unique_ptr<cosmo::IRequestDispatcher>{}; });
    REQUIRE_THROWS_AS(sut.InitHttpServer("127.0.0.1", 0), std::runtime_error);
    REQUIRE_NOTHROW(sut.StopHttpServer());
}
