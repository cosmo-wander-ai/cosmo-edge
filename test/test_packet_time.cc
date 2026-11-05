#include <array>
#include <limits>
#include <string>

#include "catch_amalgamated.hpp"
#include "media/PacketTime.h"

namespace {
using cosmo::media::PacketTime;
using cosmo::media::RescalePacketTime;
constexpr auto kMin = std::numeric_limits<int64_t>::min();
constexpr auto kMax = std::numeric_limits<int64_t>::max();

void Reject(int64_t pts, int64_t dts, int64_t duration, int num, int den, int64_t origin, int scale) {
    PacketTime output{11, 22, 33};
    std::string reason;
    CHECK_FALSE(RescalePacketTime(pts, dts, duration, num, den, origin, scale, output, reason));
    CHECK_FALSE(reason.empty());
    CHECK(output.pts == 11);
    CHECK(output.dts == 22);
    CHECK(output.duration == 33);
}
}  // namespace

TEST_CASE("Packet time preserves B-picture presentation order and a negative decode origin",
          "[media][packet-time]") {
    const std::array<int64_t, 4> pts{0, 10800, 3600, 7200};
    const std::array<int64_t, 4> dts{-7200, -3600, 0, 3600};
    const std::array<int64_t, 4> expected_pts{80, 200, 120, 160};
    for (size_t i = 0; i < pts.size(); ++i) {
        PacketTime output;
        std::string reason = "previous failure";
        REQUIRE(RescalePacketTime(pts[i], dts[i], 3600, 1, 90000, -7200, 1000, output, reason));
        CHECK(reason.empty());
        CHECK(output.pts == expected_pts[i]);
        CHECK(output.dts == static_cast<int64_t>(i) * 40);
        CHECK(output.duration == 40);
    }
}

TEST_CASE("Packet time preserves one-tick composition offsets and zero durations", "[media][packet-time]") {
    PacketTime output;
    std::string reason;
    REQUIRE(RescalePacketTime(90001, 90000, 3600, 1, 90000, 90000, 90000, output, reason));
    CHECK(output.pts == 1);
    CHECK(output.dts == 0);
    CHECK(output.duration == 3600);
    REQUIRE(RescalePacketTime(90001, 90000, 0, 1, 90000, 90000, 90000, output, reason));
    CHECK(output.duration == 0);
}

TEST_CASE("Packet duration rescaling shares adjacent rounding boundaries", "[media][packet-time]") {
    int64_t end = 0;
    for (int64_t i = 0; i < 30000; ++i) {
        PacketTime output;
        std::string reason;
        REQUIRE(RescalePacketTime(i, i, 1, 1001, 30000, 0, 1000, output, reason));
        REQUIRE(output.dts == end);
        CHECK((output.duration == 33 || output.duration == 34));
        end = output.dts + output.duration;
    }
    CHECK(end == 1001000);
}

TEST_CASE("Packet time rejects missing timestamps and invalid units without changing output",
          "[media][packet-time]") {
    Reject(kMin, 0, 1, 1, 90000, 0, 1000);
    Reject(0, kMin, 1, 1, 90000, 0, 1000);
    Reject(0, 0, 1, 1, 90000, kMin, 1000);
    Reject(0, 0, kMin, 1, 90000, 0, 1000);
    Reject(0, 0, -1, 1, 90000, 0, 1000);
    for (const int invalid : {0, -1, std::numeric_limits<int>::min()}) {
        Reject(0, 0, 1, invalid, 90000, 0, 1000);
        Reject(0, 0, 1, 1, invalid, 0, 1000);
        Reject(0, 0, 1, 1, 90000, 0, invalid);
    }
}

TEST_CASE("Packet time rejects negative composition offsets and decode timestamps before origin",
          "[media][packet-time]") {
    Reject(99, 100, 1, 1, 90000, 0, 1000);
    Reject(100, 99, 1, 1, 90000, 100, 1000);
    // A sub-output-tick negative CTS must not be hidden by rounding.
    Reject(89999, 90000, 1, 1, 90000, 0, 1);
}

TEST_CASE("Packet time detects arithmetic and FFmpeg result overflow", "[media][packet-time]") {
    Reject(kMax, 0, 0, 1, 1, -1, 1);
    Reject(0, kMin + 1, 0, 1, 1, 2, 1);
    Reject(kMax - 1, kMax - 1, 2, 1, 1, 0, 1);
    Reject(kMax, 0, 0, 2, 1, 0, 1);
    Reject(kMax, kMax, 0, 1, 1, 0, 2);
    Reject(0, 0, kMax, 1, 1, 0, 2);
    const auto int_max = std::numeric_limits<int>::max();
    Reject(3, 0, 0, int_max, 1, 0, int_max);

    PacketTime output;
    std::string reason;
    REQUIRE(RescalePacketTime(kMax, kMax, 0, 1, 1, 0, 1, output, reason));
    CHECK(output.pts == kMax);
    CHECK(output.dts == kMax);
    CHECK(output.duration == 0);
    REQUIRE(RescalePacketTime(kMin + 1, kMin + 1, 0, 1, 1, kMin + 1, 1, output, reason));
    CHECK(output.pts == 0);
    CHECK(output.dts == 0);
}
