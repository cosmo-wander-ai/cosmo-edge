#include <array>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/DecoderParameterSets.h"

namespace {
using cosmo::media::DecoderParameterSets;
using cosmo::media::VideoCodecType;

const std::vector<uint8_t> kAvcHeaders{0, 0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x68, 0x80};
const std::vector<uint8_t> kAvcConfig{1, 66, 0, 30, 0xff, 0xe1, 0, 2, 0x67, 0x42, 1, 0, 2, 0x68, 0x80};
const std::vector<uint8_t> kAvcIdr{0, 0, 0, 1, 0x06, 0x80, 0, 0, 1, 0x65, 0x88};
const std::vector<uint8_t> kHevcHeaders{0,    0, 0,    1, 0x40, 1, 0x80, 0,    0, 0,   1,
                                        0x42, 1, 0x80, 0, 0,    0, 1,    0x44, 1, 0x80};
const std::vector<uint8_t> kHevcIdr{0, 0, 1, 0x26, 1, 0x80};

std::vector<uint8_t> HevcConfig() {
    std::vector<uint8_t> data(23, 0);
    data[0]  = 1;
    data[22] = 3;
    for (unsigned type : {32u, 33u, 34u}) {
        const std::array<uint8_t, 8> array{static_cast<uint8_t>(type),      0, 1,   0, 3,
                                           static_cast<uint8_t>(type << 1), 1, 0x80};
        data.insert(data.end(), array.begin(), array.end());
    }
    return data;
}

std::vector<uint8_t> Prepare(DecoderParameterSets& sets, const std::vector<uint8_t>& packet) {
    const uint8_t* output = nullptr;
    size_t size           = 0;
    std::string error;
    REQUIRE(sets.Prepare(packet.data(), packet.size(), output, size, error));
    REQUIRE(error.empty());
    return {output, output + size};
}

std::vector<uint8_t> Join(const std::vector<uint8_t>& prefix, const std::vector<uint8_t>& packet) {
    auto result = prefix;
    result.insert(result.end(), packet.begin(), packet.end());
    return result;
}
}  // namespace

TEST_CASE("Decoder parameter sets supply SDP-only AVC and HEVC headers", "[media][decoder-headers]") {
    DecoderParameterSets sets;
    std::string error;
    SECTION("AVC Annex B") {
        REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcHeaders, error));
        CHECK(Prepare(sets, kAvcIdr) == Join(kAvcHeaders, kAvcIdr));
    }
    SECTION("AVC avcC") {
        REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcConfig, error));
        CHECK(Prepare(sets, kAvcIdr) == Join(kAvcHeaders, kAvcIdr));
    }
    SECTION("HEVC Annex B") {
        REQUIRE(sets.Configure(VideoCodecType::kH265, kHevcHeaders, error));
        CHECK(Prepare(sets, kHevcIdr) == Join(kHevcHeaders, kHevcIdr));
    }
    SECTION("HEVC hvcC") {
        REQUIRE(sets.Configure(VideoCodecType::kH265, HevcConfig(), error));
        CHECK(Prepare(sets, kHevcIdr) == Join(kHevcHeaders, kHevcIdr));
    }
}

TEST_CASE("Decoder headers are consumed only after acceptance and reset on reconnect",
          "[media][decoder-headers]") {
    DecoderParameterSets sets;
    std::string error;
    REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcConfig, error));
    const auto expected = Join(kAvcHeaders, kAvcIdr);
    CHECK(Prepare(sets, kAvcIdr) == expected);
    CHECK(Prepare(sets, kAvcIdr) == expected);  // rejected submission retains headers
    sets.Commit();
    CHECK(Prepare(sets, kAvcIdr) == kAvcIdr);
    sets.Reset();
    CHECK(Prepare(sets, kAvcIdr) == expected);
    sets.Commit();
    REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcConfig, error));
    CHECK(Prepare(sets, kAvcIdr) == expected);
}

TEST_CASE("Decoder headers preserve complete in-band packets and wait for random access",
          "[media][decoder-headers]") {
    DecoderParameterSets sets;
    std::string error;
    REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcConfig, error));
    const std::vector<uint8_t> pframe{0, 0, 1, 0x41, 0x80};
    CHECK(Prepare(sets, pframe) == pframe);
    sets.Commit();
    CHECK(Prepare(sets, kAvcIdr) == Join(kAvcHeaders, kAvcIdr));
    const auto complete = Join(kAvcHeaders, kAvcIdr);
    CHECK(Prepare(sets, complete) == complete);
    sets.Commit();
    CHECK(Prepare(sets, kAvcIdr) == kAvcIdr);
}

TEST_CASE("Decoder headers accept three-byte start codes and retain in-band updates",
          "[media][decoder-headers]") {
    DecoderParameterSets sets;
    std::string error;
    const std::vector<uint8_t> headers{0, 0, 1, 0x67, 0x42, 0, 0, 1, 0x68, 0x80};
    REQUIRE(sets.Configure(VideoCodecType::kH264, headers, error));
    const auto partial = Join({0, 0, 1, 0x68, 0x81}, kAvcIdr);
    CHECK(Prepare(sets, partial) == Join(kAvcHeaders, partial));
}

TEST_CASE("HEVC CRA and BLA access units receive out-of-band parameter sets", "[media][decoder-headers]") {
    for (unsigned type : {16u, 17u, 18u, 19u, 20u, 21u}) {
        DecoderParameterSets sets;
        std::string error;
        REQUIRE(sets.Configure(VideoCodecType::kH265, HevcConfig(), error));
        const std::vector<uint8_t> packet{0, 0, 1, static_cast<uint8_t>(type << 1), 1, 0x80};
        CHECK(Prepare(sets, packet) == Join(kHevcHeaders, packet));
    }
}

TEST_CASE("Parameter sets after the first picture cannot initialize that picture",
          "[media][decoder-headers]") {
    DecoderParameterSets sets;
    std::string error;
    SECTION("AVC") {
        REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcConfig, error));
        const auto packet = Join(kAvcIdr, kAvcHeaders);
        CHECK(Prepare(sets, packet) == Join(kAvcHeaders, packet));
    }
    SECTION("HEVC") {
        REQUIRE(sets.Configure(VideoCodecType::kH265, HevcConfig(), error));
        const auto packet = Join(kHevcIdr, kHevcHeaders);
        CHECK(Prepare(sets, packet) == Join(kHevcHeaders, packet));
    }
}

TEST_CASE("Decoder headers reject truncated incomplete and invalid configuration records",
          "[media][decoder-headers]") {
    for (const auto codec : {VideoCodecType::kH264, VideoCodecType::kH265}) {
        const auto valid = codec == VideoCodecType::kH264 ? kAvcConfig : HevcConfig();
        for (size_t size = 1; size < valid.size(); ++size) {
            DecoderParameterSets sets;
            std::string error;
            CHECK_FALSE(sets.Configure(codec, {valid.begin(), valid.begin() + size}, error));
            CHECK_FALSE(error.empty());
        }
    }
    DecoderParameterSets sets;
    std::string error;
    CHECK_FALSE(sets.Configure(VideoCodecType::kH264, {0, 0, 1, 0x67, 0x80}, error));
    CHECK_FALSE(sets.Configure(VideoCodecType::kH265, {0, 0, 1, 0x40, 0}, error));
    auto invalid = kAvcConfig;
    invalid[8]   = 0xe7;
    CHECK_FALSE(sets.Configure(VideoCodecType::kH264, invalid, error));
    CHECK_FALSE(sets.Configure(VideoCodecType::kH264, std::vector<uint8_t>(4 * 1024 * 1024 + 1), error));
    CHECK_FALSE(sets.Configure(VideoCodecType::kMjpeg, kAvcConfig, error));
}

TEST_CASE("Failed stream replacement cannot reuse previous decoder headers", "[media][decoder-headers]") {
    DecoderParameterSets sets;
    std::string error;
    REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcConfig, error));
    REQUIRE_FALSE(sets.Configure(VideoCodecType::kH264, {1, 66, 0}, error));
    CHECK(sets.HeaderBytes() == 0);
    const uint8_t* output = nullptr;
    size_t size           = 0;
    CHECK_FALSE(sets.Prepare(kAvcIdr.data(), kAvcIdr.size(), output, size, error));
    REQUIRE(sets.Configure(VideoCodecType::kH264, {}, error));
    CHECK(Prepare(sets, kAvcIdr) == kAvcIdr);
}

TEST_CASE("Decoder parameter injection rejects AVCC packets at the Annex B boundary",
          "[media][decoder-headers]") {
    DecoderParameterSets sets;
    std::string error;
    REQUIRE(sets.Configure(VideoCodecType::kH264, kAvcConfig, error));
    const std::vector<uint8_t> avcc_packet{0, 0, 0, 2, 0x65, 0x88};
    const uint8_t* output = nullptr;
    size_t size           = 0;
    CHECK_FALSE(sets.Prepare(avcc_packet.data(), avcc_packet.size(), output, size, error));
    CHECK_FALSE(sets.Prepare(nullptr, 1, output, size, error));
    CHECK_FALSE(sets.Prepare(kAvcIdr.data(), 64 * 1024 * 1024 + 1, output, size, error));
}
