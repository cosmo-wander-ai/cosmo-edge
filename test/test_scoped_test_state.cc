#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "catch_amalgamated.hpp"
#include "service/detail/ServiceRegistry.h"
#include "support/ScopedCurrentPath.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/PathUtil.h"

namespace {

class ITestPrimary {
public:
    virtual ~ITestPrimary() = default;
    virtual int Value()     = 0;
};

class ITestSecondary {
public:
    virtual ~ITestSecondary() = default;
    virtual int OtherValue()  = 0;
};

class TestService final : public ITestPrimary, public ITestSecondary {
public:
    int Value() override {
        return 7;
    }

    int OtherValue() override {
        return 11;
    }
};

class ScopedDirectoryRemoval final {
public:
    explicit ScopedDirectoryRemoval(std::filesystem::path path) : path_(std::move(path)) {}

    ~ScopedDirectoryRemoval() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

void ThrowWithServiceOverride(TestService& service) {
    cosmo::test::ScopedServiceOverride<ITestPrimary> override(service);
    throw std::runtime_error("expected test exception");
}

void ThrowWithPathOverride(const std::string& data_root, const std::string& app_root) {
    cosmo::test::ScopedPathOverride override(data_root, app_root);
    throw std::runtime_error("expected test exception");
}

void ThrowWithCurrentPath(const std::filesystem::path& path) {
    cosmo::test::ScopedCurrentPath override(path);
    throw std::runtime_error("expected test exception");
}

static_assert(!std::is_copy_constructible_v<cosmo::test::ScopedServiceOverride<ITestPrimary>>);
static_assert(!std::is_move_constructible_v<cosmo::test::ScopedServiceOverride<ITestPrimary>>);
static_assert(!std::is_copy_constructible_v<cosmo::test::ScopedPathOverride>);
static_assert(!std::is_move_constructible_v<cosmo::test::ScopedPathOverride>);
static_assert(!std::is_copy_constructible_v<cosmo::test::ScopedCurrentPath>);
static_assert(!std::is_move_constructible_v<cosmo::test::ScopedCurrentPath>);

}  // namespace

TEST_CASE("ScopedServiceOverride registers exactly one interface", "[test-isolation]") {
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    REQUIRE(registry.Size() == 0);
    TestService service;

    {
        cosmo::test::ScopedServiceOverride<ITestPrimary> primary(service);
        CHECK(registry.Has<ITestPrimary>());
        CHECK_FALSE(registry.Has<ITestSecondary>());
        CHECK(&registry.Get<ITestPrimary>() == static_cast<ITestPrimary*>(&service));
        CHECK(registry.Get<ITestPrimary>().Value() == 7);
    }

    CHECK_FALSE(registry.Has<ITestPrimary>());
    CHECK(registry.Size() == 0);
}

TEST_CASE("ScopedServiceOverride rejects replacement and supports explicit aliases", "[test-isolation]") {
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    REQUIRE(registry.Size() == 0);
    TestService first;
    TestService second;

    {
        cosmo::test::ScopedServiceOverride<ITestPrimary> primary(first);
        CHECK_THROWS_AS((cosmo::test::ScopedServiceOverride<ITestPrimary>{second}), std::logic_error);
        CHECK(&registry.Get<ITestPrimary>() == static_cast<ITestPrimary*>(&first));

        cosmo::test::ScopedServiceOverride<ITestSecondary> secondary(first);
        CHECK(registry.Get<ITestSecondary>().OtherValue() == 11);
        CHECK(registry.Size() == 2);
    }

    CHECK(registry.Size() == 0);
}

TEST_CASE("ScopedServiceOverride cleans up during stack unwinding", "[test-isolation]") {
    TestService service;

    CHECK_THROWS_AS(ThrowWithServiceOverride(service), std::runtime_error);
    CHECK_FALSE(cosmo::service::ServiceRegistry::Instance().Has<ITestPrimary>());
    CHECK(cosmo::service::ServiceRegistry::Instance().Size() == 0);
}

TEST_CASE("ScopedPathOverride restores nested and exceptional overrides", "[test-isolation]") {
    const auto original_data_root = cosmo::path::GetBaseDir();
    const auto original_app_root  = cosmo::path::GetAppBaseDir();
    const std::filesystem::path outer_root("/tmp/cosmo-scoped-path-outer");
    const std::filesystem::path inner_root("/tmp/cosmo-scoped-path-inner");
    ScopedDirectoryRemoval remove_outer(outer_root);
    ScopedDirectoryRemoval remove_inner(inner_root);

    {
        cosmo::test::ScopedPathOverride outer(outer_root.string(), (outer_root / "app").string());
        CHECK(cosmo::path::GetBaseDir() == outer_root.string());
        CHECK(cosmo::path::GetAppBaseDir() == (outer_root / "app").string());

        {
            cosmo::test::ScopedPathOverride inner(inner_root.string(), (inner_root / "app").string());
            CHECK(cosmo::path::GetBaseDir() == inner_root.string());
            CHECK(cosmo::path::GetAppBaseDir() == (inner_root / "app").string());
        }

        CHECK(cosmo::path::GetBaseDir() == outer_root.string());
        CHECK_THROWS_AS(ThrowWithPathOverride(inner_root.string(), (inner_root / "app").string()),
                        std::runtime_error);
        CHECK(cosmo::path::GetBaseDir() == outer_root.string());
    }

    CHECK(cosmo::path::GetBaseDir() == original_data_root);
    CHECK(cosmo::path::GetAppBaseDir() == original_app_root);
}

TEST_CASE("ScopedPathOverride rejects invalid roots without changing current state", "[test-isolation]") {
    const auto original_data_root = cosmo::path::GetBaseDir();
    const auto original_app_root  = cosmo::path::GetAppBaseDir();

    CHECK_THROWS_AS((cosmo::test::ScopedPathOverride{"relative/data", "/tmp/cosmo-app"}),
                    std::invalid_argument);
    CHECK(cosmo::path::GetBaseDir() == original_data_root);
    CHECK(cosmo::path::GetAppBaseDir() == original_app_root);
}

TEST_CASE("ScopedCurrentPath restores normal and exceptional scopes", "[test-isolation]") {
    const auto original = std::filesystem::current_path();
    const auto root     = std::filesystem::path("/tmp/cosmo-scoped-current-path");
    ScopedDirectoryRemoval cleanup(root);
    std::filesystem::create_directories(root);

    {
        cosmo::test::ScopedCurrentPath current_path(root);
        CHECK(std::filesystem::current_path() == root);
    }
    CHECK(std::filesystem::current_path() == original);

    CHECK_THROWS_AS(ThrowWithCurrentPath(root), std::runtime_error);
    CHECK(std::filesystem::current_path() == original);
}
