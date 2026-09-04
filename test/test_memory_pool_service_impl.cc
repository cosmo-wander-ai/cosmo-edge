#include "catch_amalgamated.hpp"
/*
 * test_memory_pool_service_impl.cc — MemoryPoolServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: MemoryPoolServiceImpl wraps VPU device memory (bmlib).
 * Construction requires bmlib device init which only works on aarch64.
 * All tests tagged [.device].
 */
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

#include "mem/DeviceContext.h"
#include "mem/IDeviceContext.h"
#include "service/infra/impl/MemoryPoolServiceImpl.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;

namespace {

using namespace std::chrono_literals;

class ScopedDeviceContext {
public:
    ScopedDeviceContext()
        : device_context_(std::make_unique<cosmo::mem::DeviceContext>()), registration_(*device_context_) {}

    ScopedDeviceContext(const ScopedDeviceContext&)            = delete;
    ScopedDeviceContext& operator=(const ScopedDeviceContext&) = delete;

private:
    std::unique_ptr<cosmo::mem::DeviceContext> device_context_;
    cosmo::test::ScopedServiceOverride<cosmo::mem::IDeviceContext> registration_;
};

class MemoryPoolFixture {
public:
    MemoryPoolFixture() = default;

    MemoryPoolFixture(const MemoryPoolFixture&)            = delete;
    MemoryPoolFixture& operator=(const MemoryPoolFixture&) = delete;

    MemoryPoolServiceImpl& Service() {
        return service_;
    }

private:
    ScopedDeviceContext device_context_;
    MemoryPoolServiceImpl service_;
};

}  // namespace

TEST_CASE("MemoryPoolServiceImpl: construction and destruction", "[MemoryPoolService][.device]") {
    REQUIRE_NOTHROW([]() { MemoryPoolFixture fixture; }());
}

TEST_CASE("MemoryPoolServiceImpl: Status returns pool info", "[MemoryPoolService][.device]") {
    MemoryPoolFixture fixture;
    auto& sut   = fixture.Service();
    auto status = sut.Status();
    // Should return at least some pool buckets
    REQUIRE(!status.empty());
}

TEST_CASE("MemoryPoolServiceImpl: OutputMallocBuf returns non-empty string", "[MemoryPoolService][.device]") {
    MemoryPoolFixture fixture;
    auto& sut = fixture.Service();
    auto buf  = sut.OutputMallocBuf();
    REQUIRE(!buf.empty());
}

TEST_CASE("MemoryPoolServiceImpl: Acquire and Recycle", "[MemoryPoolService][.device]") {
    MemoryPoolFixture fixture;
    auto& sut = fixture.Service();

    SECTION("Acquire returns non-null block for valid size") {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        cosmo::mem::Block* block{nullptr};
        while (block == nullptr && std::chrono::steady_clock::now() < deadline) {
            block = sut.Acquire(1024);
            if (block == nullptr) {
                std::this_thread::sleep_for(10ms);
            }
        }
        REQUIRE(block != nullptr);
        sut.Recycle(block);
    }

    SECTION("Recycle nullptr does not crash") {
        REQUIRE_NOTHROW(sut.Recycle(nullptr));
    }
}
