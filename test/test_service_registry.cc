#include <memory>
#include <string>

#include "catch_amalgamated.hpp"
#include "service/detail/ServiceRegistry.h"

// Minimal test interfaces — lightweight, no mock framework needed
namespace {

class ITestServiceA {
public:
    virtual ~ITestServiceA()         = default;
    virtual std::string Name() const = 0;
    virtual int Value() const        = 0;
};

class ITestServiceB {
public:
    virtual ~ITestServiceB()        = default;
    virtual std::string Tag() const = 0;
};

class TestServiceAImpl : public ITestServiceA {
public:
    explicit TestServiceAImpl(std::string name, int val) : name_(std::move(name)), val_(val) {}
    std::string Name() const override {
        return name_;
    }
    int Value() const override {
        return val_;
    }

private:
    std::string name_;
    int val_;
};

class TestServiceBImpl : public ITestServiceB {
public:
    explicit TestServiceBImpl(std::string tag) : tag_(std::move(tag)) {}
    std::string Tag() const override {
        return tag_;
    }

private:
    std::string tag_;
};

// RAII guard to clean up registry after each test
struct RegistryGuard {
    ~RegistryGuard() {
        cosmo::service::ServiceRegistry::Instance().Set<ITestServiceA>(nullptr);
        cosmo::service::ServiceRegistry::Instance().Set<ITestServiceB>(nullptr);
    }
};

}  // namespace

TEST_CASE("ServiceRegistry: Set/Get round-trip", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();

    SECTION("Set then Get returns same instance") {
        TestServiceAImpl impl("hello", 42);
        registry.Set<ITestServiceA>(&impl);
        auto& retrieved = registry.Get<ITestServiceA>();
        REQUIRE(retrieved.Name() == "hello");
        REQUIRE(retrieved.Value() == 42);
        REQUIRE(&retrieved == &impl);
    }

    SECTION("Set nullptr then Has returns false") {
        TestServiceAImpl impl("temp", 0);
        registry.Set<ITestServiceA>(&impl);
        REQUIRE(registry.Has<ITestServiceA>() == true);

        registry.Set<ITestServiceA>(nullptr);
        REQUIRE(registry.Has<ITestServiceA>() == false);
    }

    SECTION("Has returns false for unregistered service") {
        // ITestServiceB was never registered (RegistryGuard ensures cleanup)
        REQUIRE(registry.Has<ITestServiceB>() == false);
    }
}

TEST_CASE("ServiceRegistry: Multiple services coexist", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();

    TestServiceAImpl implA("serviceA", 1);
    TestServiceBImpl implB("serviceB");

    registry.Set<ITestServiceA>(&implA);
    registry.Set<ITestServiceB>(&implB);

    SECTION("Both services retrievable independently") {
        REQUIRE(registry.Get<ITestServiceA>().Name() == "serviceA");
        REQUIRE(registry.Get<ITestServiceB>().Tag() == "serviceB");
    }

    SECTION("Size reflects registered count") {
        auto initialSize = registry.Size();
        // Note: other tests may have registered services, so we just check >= 2
        REQUIRE(initialSize >= 2);
    }
}

TEST_CASE("ServiceRegistry: Set overwrites previous registration", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();

    TestServiceAImpl impl1("first", 1);
    TestServiceAImpl impl2("second", 2);

    registry.Set<ITestServiceA>(&impl1);
    REQUIRE(registry.Get<ITestServiceA>().Name() == "first");

    registry.Set<ITestServiceA>(&impl2);
    REQUIRE(registry.Get<ITestServiceA>().Name() == "second");
    REQUIRE(registry.Get<ITestServiceA>().Value() == 2);
}
