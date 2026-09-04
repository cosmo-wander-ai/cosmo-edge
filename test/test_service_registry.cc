#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

class ITestServiceC {
public:
    virtual ~ITestServiceC() = default;
    virtual int Id() const   = 0;
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

class DestructionOrderServiceA : public TestServiceAImpl {
public:
    DestructionOrderServiceA(std::string name, std::vector<std::string>* destruction_order)
        : TestServiceAImpl(std::move(name), 1), destruction_order_(destruction_order) {}

    ~DestructionOrderServiceA() override {
        destruction_order_->push_back(Name());
    }

private:
    std::vector<std::string>* destruction_order_;
};

class DestructionOrderServiceB : public TestServiceBImpl {
public:
    DestructionOrderServiceB(std::string tag, std::vector<std::string>* destruction_order)
        : TestServiceBImpl(std::move(tag)), destruction_order_(destruction_order) {}

    ~DestructionOrderServiceB() override {
        destruction_order_->push_back(Tag());
    }

private:
    std::vector<std::string>* destruction_order_;
};

struct DestructorCallbackState {
    cosmo::service::ServiceRegistry::LifecycleState lifecycle_state{
        cosmo::service::ServiceRegistry::LifecycleState::kRegistering};
    size_t registry_size = 1;
    bool has_service     = true;
    bool get_failed      = false;
};

class DestructorCallbackService : public ITestServiceC {
public:
    explicit DestructorCallbackService(DestructorCallbackState* state) : state_(state) {}

    ~DestructorCallbackService() override {
        auto& registry          = cosmo::service::ServiceRegistry::Instance();
        state_->lifecycle_state = registry.GetLifecycleState();
        state_->registry_size   = registry.Size();
        state_->has_service     = registry.Has<ITestServiceA>();
        try {
            static_cast<void>(registry.Get<ITestServiceA>());
        } catch (const std::logic_error&) {
            state_->get_failed = true;
        }
    }

    int Id() const override {
        return 3;
    }

private:
    DestructorCallbackState* state_;
};

struct BlockingDestructorState {
    std::mutex mutex;
    std::condition_variable cv;
    bool destructor_entered = false;
    bool release_destructor = false;
    int destruction_count   = 0;
};

class BlockingDestructorService : public ITestServiceC {
public:
    explicit BlockingDestructorService(std::shared_ptr<BlockingDestructorState> state)
        : state_(std::move(state)) {}

    ~BlockingDestructorService() override {
        std::unique_lock lock(state_->mutex);
        state_->destructor_entered = true;
        state_->cv.notify_all();
        state_->cv.wait(lock, [this]() { return state_->release_destructor; });
        ++state_->destruction_count;
    }

    int Id() const override {
        return 3;
    }

private:
    std::shared_ptr<BlockingDestructorState> state_;
};

class OffsetBase {
public:
    virtual ~OffsetBase() = default;
    virtual int OffsetValue() const {
        return 7;
    }

private:
    int padding_[4]{1, 2, 3, 4};
};

class OffsetServiceA : public OffsetBase, public ITestServiceA {
public:
    std::string Name() const override {
        return "offset";
    }

    int Value() const override {
        return OffsetValue();
    }
};

// RAII guard to clean up registry after each test
struct RegistryGuard {
    RegistryGuard() {
        const auto& registry = cosmo::service::ServiceRegistry::Instance();
        if (registry.GetLifecycleState() != cosmo::service::ServiceRegistry::LifecycleState::kRegistering ||
            registry.Size() != 0) {
            throw std::logic_error("service registry was not clean before registry contract test");
        }
    }

    ~RegistryGuard() {
        cosmo::service::ServiceRegistry::Instance().ShutdownAll();
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

    SECTION("Get rejects an unregistered service") {
        REQUIRE_THROWS_AS(registry.Get<ITestServiceB>(), std::logic_error);
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

TEST_CASE("ServiceRegistry: Registration lifecycle is explicit", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    TestServiceAImpl impl("ready", 1);
    TestServiceBImpl late_impl("late");

    registry.Set<ITestServiceA>(&impl);
    registry.CompleteRegistration();

    REQUIRE(registry.GetLifecycleState() == cosmo::service::ServiceRegistry::LifecycleState::kInitialized);
    REQUIRE_THROWS_AS(registry.Set<ITestServiceB>(&late_impl), std::logic_error);

    registry.ShutdownAll();
    REQUIRE(registry.GetLifecycleState() == cosmo::service::ServiceRegistry::LifecycleState::kRegistering);
    REQUIRE(registry.Size() == 0);
}

TEST_CASE("ServiceRegistry: Duplicate owned registration is rejected", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    std::vector<std::string> destruction_order;

    registry.Register<ITestServiceA>(std::make_unique<DestructionOrderServiceA>("first", &destruction_order));
    REQUIRE_THROWS_AS(registry.Register<ITestServiceA>(
                          std::make_unique<DestructionOrderServiceA>("duplicate", &destruction_order)),
                      std::logic_error);

    REQUIRE(registry.Size() == 1);
    REQUIRE(registry.Get<ITestServiceA>().Name() == "first");
    REQUIRE(destruction_order == std::vector<std::string>{"duplicate"});

    registry.ShutdownAll();
    REQUIRE((destruction_order == std::vector<std::string>{"duplicate", "first"}));
}

TEST_CASE("ServiceRegistry: Owned registration cannot be masked by Set", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    TestServiceAImpl replacement("replacement", 2);

    registry.Register<ITestServiceA>(std::make_unique<TestServiceAImpl>("owned", 1));

    REQUIRE_THROWS_AS(registry.Set<ITestServiceA>(&replacement), std::logic_error);
    REQUIRE_THROWS_AS(registry.Set<ITestServiceA>(nullptr), std::logic_error);
    REQUIRE(registry.Get<ITestServiceA>().Name() == "owned");
}

TEST_CASE("ServiceRegistry: Owned services are destroyed in reverse registration order",
          "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    std::vector<std::string> destruction_order;

    registry.Register<ITestServiceA>(std::make_unique<DestructionOrderServiceA>("first", &destruction_order));
    registry.Register<ITestServiceB>(
        std::make_unique<DestructionOrderServiceB>("second", &destruction_order));
    registry.CompleteRegistration();
    registry.ShutdownAll();

    REQUIRE((destruction_order == std::vector<std::string>{"second", "first"}));
}

TEST_CASE("ServiceRegistry: Destructors can inspect shutdown state without deadlock", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    DestructorCallbackState callback_state;

    registry.Register<ITestServiceA>(std::make_unique<TestServiceAImpl>("dependency", 1));
    registry.Register<ITestServiceC>(std::make_unique<DestructorCallbackService>(&callback_state));
    registry.CompleteRegistration();
    registry.ShutdownAll();

    REQUIRE(callback_state.lifecycle_state == cosmo::service::ServiceRegistry::LifecycleState::kShuttingDown);
    REQUIRE(callback_state.registry_size == 0);
    REQUIRE_FALSE(callback_state.has_service);
    REQUIRE(callback_state.get_failed);
}

TEST_CASE("ServiceRegistry: Concurrent shutdown is serialized and rejects new lookups", "[ServiceRegistry]") {
    using namespace std::chrono_literals;

    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();
    auto state     = std::make_shared<BlockingDestructorState>();
    registry.Register<ITestServiceC>(std::make_unique<BlockingDestructorService>(state));
    registry.CompleteRegistration();

    std::thread first_shutdown([&registry]() { registry.ShutdownAll(); });
    {
        std::unique_lock lock(state->mutex);
        state->cv.wait(lock, [&state]() { return state->destructor_entered; });
    }

    const auto lifecycle_state = registry.GetLifecycleState();
    bool get_rejected          = false;
    try {
        static_cast<void>(registry.Get<ITestServiceC>());
    } catch (const std::logic_error&) {
        get_rejected = true;
    }
    bool registration_rejected = false;
    try {
        registry.Register<ITestServiceB>(std::make_unique<TestServiceBImpl>("late"));
    } catch (const std::logic_error&) {
        registration_rejected = true;
    }

    std::atomic<bool> second_started{false};
    std::atomic<bool> second_returned{false};
    std::thread second_shutdown([&registry, &second_started, &second_returned]() {
        second_started.store(true, std::memory_order_release);
        registry.ShutdownAll();
        second_returned.store(true, std::memory_order_release);
    });
    while (!second_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(20ms);
    const bool second_waited = !second_returned.load(std::memory_order_acquire);

    {
        std::lock_guard lock(state->mutex);
        state->release_destructor = true;
    }
    state->cv.notify_all();
    first_shutdown.join();
    second_shutdown.join();

    REQUIRE(lifecycle_state == cosmo::service::ServiceRegistry::LifecycleState::kShuttingDown);
    REQUIRE(get_rejected);
    REQUIRE(registration_rejected);
    REQUIRE(second_waited);
    REQUIRE(second_returned.load(std::memory_order_acquire));
    REQUIRE(state->destruction_count == 1);
    REQUIRE(registry.GetLifecycleState() == cosmo::service::ServiceRegistry::LifecycleState::kRegistering);
}

TEST_CASE("ServiceRegistry: Owned interface pointers preserve base adjustment", "[ServiceRegistry]") {
    RegistryGuard guard;
    auto& registry = cosmo::service::ServiceRegistry::Instance();

    registry.Register<ITestServiceA>(std::make_unique<OffsetServiceA>());

    REQUIRE(registry.Get<ITestServiceA>().Name() == "offset");
    REQUIRE(registry.Get<ITestServiceA>().Value() == 7);
}
