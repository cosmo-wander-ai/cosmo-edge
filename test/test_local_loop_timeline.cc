#include <array>
#include <limits>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/LocalLoopCodecGuard.h"
#include "media/LocalLoopTimeline.h"
#include "media/TimestampMath.h"

using cosmo::media::LocalLoopInput;
using cosmo::media::LocalLoopTimeline;

TEST_CASE("Local replay preserves decode order and B-frame composition offsets", "[media][local-loop]") {
    LocalLoopTimeline clock;
    std::string reason;
    int64_t last = std::numeric_limits<int64_t>::min();
    const std::array<int64_t, 3> dts{0, 40, 80};
    const std::array<int64_t, 3> pts{80, 160, 80};
    for (int loop = 0; loop < 3; ++loop) {
        for (size_t i = 0; i < dts.size(); ++i) {
            LocalLoopInput in{pts[i], dts[i], 40, 1, 1000, loop > 0 && i == 0, i == 0, false};
            LocalLoopTimeline::Prepared out;
            REQUIRE(clock.Prepare(in, out, reason));
            CHECK(out.dts > last);
            CHECK(out.pts - out.dts == pts[i] - dts[i]);
            CHECK(out.dts == dts[i] + loop * 120);
            REQUIRE(clock.Commit(out, reason));
            last = out.dts;
            CHECK_FALSE(clock.Commit(out, reason));
        }
    }
}

TEST_CASE("Local replay rejects unsafe clocks and boundaries without mutating state", "[media][local-loop]") {
    const auto missing = std::numeric_limits<int64_t>::min();
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    REQUIRE_FALSE(clock.Prepare({0, 0, 1, 1, 25, false, false, false}, out, reason));
    REQUIRE(clock.Prepare({2, 0, 1, 1, 25, false, true, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE_FALSE(clock.Prepare({missing, 1, 1, 1, 25, false, false, false}, out, reason));
    REQUIRE_FALSE(clock.Prepare({3, 1, 1, 1, 30, false, false, false}, out, reason));
    REQUIRE_FALSE(clock.Prepare({0, 0, 1, 1, 25, true, false, false}, out, reason));
    REQUIRE(clock.Prepare({3, 1, 1, 1, 25, false, false, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({2, 0, 1, 1, 25, true, true, false}, out, reason));
    CHECK(out.dts == 2);
    CHECK(out.pts == 4);
    REQUIRE(clock.Commit(out, reason));
    // The first new-loop IDR is safe, but a later packet may still overlap
    // the previous loop's highest presentation timestamp.
    CHECK_FALSE(clock.Prepare({1, 1, 1, 1, 25, false, false, false}, out, reason));

    LocalLoopTimeline extreme;
    REQUIRE_FALSE(extreme.Prepare({std::numeric_limits<int64_t>::max(), -2, 1, 1, 25, false, true, false},
                                  out, reason));
}

TEST_CASE("Local replay refuses a seam requiring unbounded composition drift", "[media][local-loop]") {
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    for (int i = 0; i < 242; ++i) {
        const LocalLoopInput input{
            i == 241 ? 124928 : i * 512, i * 512 - 1024, 512, 1, 12288, false, i == 0, false};
        REQUIRE(clock.Prepare(input, out, reason));
        REQUIRE(clock.Commit(out, reason));
    }
    LocalLoopInput repeated{0, -1024, 512, 1, 12288, true, true, false};
    CHECK_FALSE(clock.Prepare(repeated, out, reason));
    CHECK(reason == "loop composition drift requires a new generation");
}

TEST_CASE("Local replay rejects unrepresentable presentation end and unknown seam duration",
          "[media][local-loop]") {
    const auto max = std::numeric_limits<int64_t>::max();
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    CHECK_FALSE(clock.Prepare({max, max - 1, 1, 1, 25, false, true, false}, out, reason));
    REQUIRE(clock.Prepare({2, 0, 0, 1, 25, false, true, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    CHECK_FALSE(clock.Prepare({2, 0, 1, 1, 25, true, true, false}, out, reason));

    LocalLoopTimeline offset_overflow;
    REQUIRE(offset_overflow.Prepare({max - 5, max - 20, 1, 1, 25, false, true, false}, out, reason));
    REQUIRE(offset_overflow.Commit(out, reason));
    CHECK_FALSE(offset_overflow.Prepare({-10, -10, 1, 1, 25, true, true, false}, out, reason));
    CHECK(reason == "loop presentation offset overflows");
    CHECK(offset_overflow.Prepare({max - 4, max - 19, 1, 1, 25, false, false, false}, out, reason));

    LocalLoopTimeline estimate_overflow;
    REQUIRE(estimate_overflow.Prepare({max - 1, max - 10, 0, 1, 25, false, true, false}, out, reason));
    REQUIRE(estimate_overflow.Commit(out, reason));
    REQUIRE(estimate_overflow.Prepare({max - 3, max - 8, 1, 1, 25, false, false, false}, out, reason));
    REQUIRE(estimate_overflow.Commit(out, reason));
    CHECK_FALSE(estimate_overflow.Prepare({0, 0, 1, 1, 25, true, true, false}, out, reason));
    CHECK(reason == "loop presentation end overflows");
}

TEST_CASE("Local replay rejects later B picture overlapping previous presentation duration",
          "[media][local-loop]") {
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    REQUIRE(clock.Prepare({100, 90, 10, 1, 1000, false, true, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({100, 90, 10, 1, 1000, true, true, false}, out, reason));
    CHECK(out.pts == 110);
    REQUIRE(clock.Commit(out, reason));
    // Decode order advances while this B picture has a presentation time
    // after the old PTS but before its known presentation end.
    REQUIRE_FALSE(clock.Prepare({95, 91, 10, 1, 1000, false, false, false}, out, reason));
}

TEST_CASE("Local replay marks estimates for any missing presentation duration", "[media][local-loop]") {
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    REQUIRE(clock.Prepare({100, 90, 0, 1, 1000, false, true, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({105, 100, 1, 1, 1000, false, false, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({100, 90, 10, 1, 1000, true, true, false}, out, reason));
    CHECK(out.usedDurationFallback);
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({105, 100, 1, 1, 1000, false, false, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({100, 90, 1, 1, 1000, true, true, false}, out, reason));
    CHECK_FALSE(out.usedDurationFallback);
}

TEST_CASE("Local replay rejects a known decode duration mismatch at the seam", "[media][local-loop]") {
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    REQUIRE(clock.Prepare({0, 0, 10, 1, 1000, false, true, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({12, 12, 10, 1, 1000, false, false, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    CHECK_FALSE(clock.Prepare({0, 0, 10, 1, 1000, true, true, false}, out, reason));
    CHECK(reason == "source duration differs from decode interval; new generation required");
}

TEST_CASE("Local replay bounds composition offsets for both muxers", "[media][local-loop]") {
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    REQUIRE(clock.Prepare({0x7ffffe, 0, 1, 1, 1000, false, true, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    CHECK(clock.Prepare({0x7fffff, 1, 1, 1, 1000, false, false, false}, out, reason));
    CHECK_FALSE(clock.Prepare({0x800000, 1, 1, 1, 1000, false, false, false}, out, reason));
    CHECK(reason == "adjusted composition offset exceeds FLV or MP4 limits");
}

TEST_CASE("Local replay tail duration fallback and explicit forced CFR", "[media][local-loop]") {
    LocalLoopTimeline clock;
    LocalLoopTimeline::Prepared out;
    std::string reason;
    REQUIRE(clock.Prepare({0, 0, 0, 1, 25, false, true, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    CHECK_FALSE(clock.Prepare({0, 0, 0, 1, 25, true, true, false}, out, reason));
    REQUIRE(clock.Prepare({1, 1, 0, 1, 25, false, false, false}, out, reason));
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({0, 0, 0, 1, 25, true, true, false}, out, reason));
    CHECK(out.usedDurationFallback);
    CHECK(out.dts == 2);
    clock.Reset();
    const auto missing = std::numeric_limits<int64_t>::min();
    REQUIRE(clock.Prepare({missing, missing, 0, 0, 0, false, true, true}, out, reason));
    CHECK(out.syntheticCfr);
    REQUIRE(clock.Commit(out, reason));
    REQUIRE(clock.Prepare({missing, missing, 0, 0, 0, true, true, true}, out, reason));
    CHECK(out.timeBaseNum == 0);
    CHECK(out.timeBaseDen == 0);
    CHECK_FALSE(clock.Prepare({10, 10, 1, 1, 25, true, true, false}, out, reason));
}

TEST_CASE("Local replay requires stable complete in-band IDR parameters", "[media][local-loop]") {
    using cosmo::media::VideoCodecType;
    cosmo::media::LocalLoopCodecGuard guard;
    const std::vector<uint8_t> idr{0, 0,    0,    1,    0x67, 0x42, 0x01, 0x0a, 0,   0,
                                   1, 0x68, 0xce, 0x01, 0,    0,    1,    0x65, 0x88};
    const std::vector<uint8_t> p{0, 0, 1, 0x41, 0x99};
    REQUIRE(guard.Observe(VideoCodecType::kH264, idr.data(), idr.size(), true));
    REQUIRE(guard.Observe(VideoCodecType::kH264, p.data(), p.size(), false));
    REQUIRE(guard.Observe(VideoCodecType::kH264, idr.data(), idr.size(), true));
    auto changed = idr;
    changed[7] ^= 1;
    CHECK_FALSE(guard.Observe(VideoCodecType::kH264, changed.data(), changed.size(), true));
    CHECK_FALSE(guard.Eligible());
    guard.Reset();
    CHECK_FALSE(guard.Observe(VideoCodecType::kH264, p.data(), p.size(), true));
    const std::vector<uint8_t> reversed{0,    0, 1,    0x65, 0x88, 0, 0,    1,   0x67,
                                        0x42, 1, 0x0a, 0,    0,    1, 0x68, 0xce};
    guard.Reset();
    CHECK_FALSE(guard.Observe(VideoCodecType::kH264, reversed.data(), reversed.size(), true));
    auto trailing = idr;
    trailing.insert(trailing.end(), {0, 0, 1});
    guard.Reset();
    CHECK_FALSE(guard.Observe(VideoCodecType::kH264, trailing.data(), trailing.size(), true));
    const std::vector<uint8_t> hevc{0, 0, 1, 0x40, 1,    2, 3, 0, 0, 1,    0x42, 1, 2,
                                    3, 0, 0, 1,    0x44, 1, 0, 0, 1, 0x26, 1,    3};
    guard.Reset();
    CHECK(guard.Observe(VideoCodecType::kH265, hevc.data(), hevc.size(), true));
    auto leading_p = hevc;
    leading_p.insert(leading_p.begin(), {0, 0, 1, 0x02, 1, 3});
    guard.Reset();
    CHECK_FALSE(guard.Observe(VideoCodecType::kH265, leading_p.data(), leading_p.size(), true));
}

TEST_CASE("Local codec side data must match the open filter configuration", "[media][local-loop]") {
    using cosmo::media::LocalLoopCodecGuard;
    const std::vector<uint8_t> config{1, 2, 3};
    auto changed   = config;
    changed.back() = 4;
    CHECK(LocalLoopCodecGuard::UnchangedExtradata(&config, config.data(), config.size()));
    CHECK_FALSE(LocalLoopCodecGuard::UnchangedExtradata(&config, changed.data(), changed.size()));
    CHECK_FALSE(LocalLoopCodecGuard::UnchangedExtradata(&config, nullptr, config.size()));
    CHECK_FALSE(LocalLoopCodecGuard::UnchangedExtradata(&config, config.data(), 0));
    CHECK_FALSE(LocalLoopCodecGuard::UnchangedExtradata(nullptr, config.data(), config.size()));
}

TEST_CASE("Timestamp differences reject missing and overflow before demux accounting",
          "[media][local-loop]") {
    int64_t delta  = 123;
    const auto min = std::numeric_limits<int64_t>::min();
    const auto max = std::numeric_limits<int64_t>::max();
    CHECK_FALSE(cosmo::media::TryTimestampDifference(min, 0, delta));
    CHECK_FALSE(cosmo::media::TryTimestampDifference(max, -2, delta));
    CHECK_FALSE(cosmo::media::TryTimestampDifference(min + 1, 2, delta));
    CHECK(delta == 123);
    CHECK(cosmo::media::TryTimestampDifference(4, -2, delta));
    CHECK(delta == 6);
}
