#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/utils/rknn_bound_input_padding.h"

namespace {
class PaddingProvider final : public cosmo::nn::RknnBoundInputProvider {
public:
    bool EnsureRgaBoundInput(int, int, std::string&) override {
        return false;
    }
};

struct PaddingFixture {
    PaddingProvider provider;
    std::array<uint8_t, 32> storage;
    cosmo::nn::RknnBoundInputTarget target;
    PaddingFixture() {
        storage.fill(0xa5);
        target.owner           = &provider;
        target.virtual_address = storage.data() + 3;
        target.fd              = 7;
        target.bytes           = 26;
        target.height          = 2;
        target.width           = 2;
        target.channels        = 3;
        target.width_stride    = 4;
        target.generation      = 9;
        target.frame_ready     = true;
    }
};
}  // namespace

TEST_CASE("CPU RGA bound padding is restricted to the RK3588 platform", "[nn][rknn][bound-padding]") {
    using cosmo::nn::UseCpuRgaBoundPadding;
    CHECK(UseCpuRgaBoundPadding("rk3588"));
    for (const auto* chip : {"rk3576", "rv1126b", "bm1688", "cv186x", "", "unspecified"}) {
        CHECK_FALSE(UseCpuRgaBoundPadding(chip));
    }
    PaddingProvider unsupported;
    std::string reason;
    CHECK_FALSE(unsupported.FillRgaBoundInput(9, {114, 114, 114}, reason));
    CHECK_FALSE(reason.empty());
}

TEST_CASE("Bound RGB padding synchronizes both directions and preserves buffer identity",
          "[nn][rknn][bound-padding]") {
    PaddingFixture fixture;
    const auto color = GENERATE(std::array<uint8_t, 3>{114, 114, 114}, std::array<uint8_t, 3>{17, 83, 201});
    std::vector<bool> sync_calls;
    std::string reason = "previous failure";
    const auto sync    = [&](bool from_device, std::string&) {
        sync_calls.push_back(from_device);
        CHECK_FALSE(fixture.target.frame_ready);
        if (from_device) {
            CHECK(std::all_of(fixture.storage.begin(), fixture.storage.end(),
                                 [](auto v) { return v == 0xa5; }));
        } else {
            for (size_t index = 0; index < 24; ++index)
                CHECK(fixture.storage[3 + index] == color[index % 3]);
        }
        return true;
    };
    REQUIRE(cosmo::nn::FillRknnBoundRgbPadding(fixture.target, &fixture.provider, 9, color, sync, reason));
    CHECK((sync_calls == std::vector<bool>{true, false}));
    CHECK(reason.empty());
    CHECK_FALSE(fixture.target.frame_ready);
    CHECK(fixture.target.owner == &fixture.provider);
    CHECK(fixture.target.generation == 9);
    CHECK(fixture.target.fd == 7);
    CHECK(fixture.target.virtual_address == fixture.storage.data() + 3);
    for (size_t index = 0; index < fixture.storage.size(); ++index) {
        if (index < 3 || index >= 27)
            CHECK(fixture.storage[index] == 0xa5);
    }
}

TEST_CASE("Bound RGB padding is restored after each native INT8 transform", "[nn][rknn][bound-padding]") {
    PaddingFixture fixture;
    int sync_calls  = 0;
    const auto sync = [&](bool, std::string&) {
        ++sync_calls;
        return true;
    };
    std::string reason;
    for (int frame = 0; frame < 3; ++frame) {
        REQUIRE(cosmo::nn::FillRknnBoundRgbPadding(fixture.target, &fixture.provider, 9, {114, 114, 114},
                                                   sync, reason));
        for (size_t index = 3; index < 27; ++index) {
            CHECK(fixture.storage[index] == 114);
            fixture.storage[index] ^= 0x80;
        }
        fixture.target.frame_ready = true;
    }
    CHECK(sync_calls == 6);
}

TEST_CASE("Bound RGB padding rejects stale or invalid allocations before cache access",
          "[nn][rknn][bound-padding]") {
    PaddingFixture fixture;
    PaddingProvider other_provider;
    auto* owner         = &fixture.provider;
    uint64_t generation = 9;
    SECTION("another owner") {
        owner = &other_provider;
    }
    SECTION("stale generation") {
        generation = 8;
    }
    SECTION("unpublished generation") {
        generation = 0;
    }
    SECTION("null mapping") {
        fixture.target.virtual_address = nullptr;
    }
    SECTION("missing fd") {
        fixture.target.fd = -1;
    }
    SECTION("invalid height") {
        fixture.target.height = 0;
    }
    SECTION("invalid width") {
        fixture.target.width = -1;
    }
    SECTION("invalid channels") {
        fixture.target.channels = 4;
    }
    SECTION("stride smaller than width") {
        fixture.target.width_stride = 1;
    }
    SECTION("allocation too small") {
        fixture.target.bytes = 23;
    }
    SECTION("large dimensions exceed allocation") {
        fixture.target.height       = std::numeric_limits<int>::max();
        fixture.target.width_stride = std::numeric_limits<int>::max();
    }
    bool synced     = false;
    const auto sync = [&](bool, std::string&) {
        synced = true;
        return true;
    };
    std::string reason;
    CHECK_FALSE(
        cosmo::nn::FillRknnBoundRgbPadding(fixture.target, owner, generation, {114, 114, 114}, sync, reason));
    CHECK_FALSE(synced);
    CHECK_FALSE(reason.empty());
    CHECK(std::all_of(fixture.storage.begin(), fixture.storage.end(), [](auto v) { return v == 0xa5; }));
    if (owner == &fixture.provider)
        CHECK_FALSE(fixture.target.frame_ready);
}

TEST_CASE("Bound RGB padding never marks a frame ready after a cache sync failure",
          "[nn][rknn][bound-padding]") {
    const bool fail_from_device = GENERATE(true, false);
    PaddingFixture fixture;
    std::vector<bool> sync_calls;
    std::string reason;
    const auto sync = [&](bool from_device, std::string& error) {
        sync_calls.push_back(from_device);
        if (from_device == fail_from_device) {
            error = "synthetic cache sync error";
            return false;
        }
        return true;
    };
    CHECK_FALSE(cosmo::nn::FillRknnBoundRgbPadding(fixture.target, &fixture.provider, 9, {114, 114, 114},
                                                   sync, reason));
    CHECK(reason == "synthetic cache sync error");
    CHECK_FALSE(fixture.target.frame_ready);
    CHECK(sync_calls.size() == (fail_from_device ? 1 : 2));
    if (fail_from_device)
        CHECK(std::all_of(fixture.storage.begin(), fixture.storage.end(), [](auto v) { return v == 0xa5; }));
    // A later retry keeps the same allocation and has to perform both syncs again.
    std::vector<bool> retried;
    const auto successful = [&](bool from_device, std::string&) {
        retried.push_back(from_device);
        return true;
    };
    REQUIRE(cosmo::nn::FillRknnBoundRgbPadding(fixture.target, &fixture.provider, 9, {114, 114, 114},
                                               successful, reason));
    CHECK((retried == std::vector<bool>{true, false}));
    CHECK_FALSE(fixture.target.frame_ready);
    CHECK(fixture.target.generation == 9);
}
