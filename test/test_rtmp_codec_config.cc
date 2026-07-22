#include <climits>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/stream/RtmpCodecConfig.h"

namespace cosmo {
namespace {

    std::vector<std::uint8_t> ValidAvcC() {
        return {1, 66, 0, 30, 0xff, 0xe1, 0, 2, 0x67, 0x42, 1, 0, 1, 0x68};
    }

    std::vector<std::uint8_t> ValidHevcC() {
        std::vector<std::uint8_t> data(23, 0);
        data[0]                 = 1;
        data[22]                = 3;
        const auto append_array = [&](std::uint8_t type, std::uint8_t first, std::uint8_t second) {
            data.push_back(type);
            data.push_back(0);
            data.push_back(1);
            data.push_back(0);
            data.push_back(2);
            data.push_back(first);
            data.push_back(second);
        };
        append_array(32, 0x40, 0x01);
        append_array(33, 0x42, 0x01);
        append_array(34, 0x44, 0x01);
        return data;
    }

}  // namespace

TEST_CASE("RtmpCodecConfig rejects truncated packets and malformed extradata", "[rtmp][codec][security]") {
    RtmpCodecConfig config(media::VideoCodecType::kH264, 1920, 1080, 25.0F);
    const std::uint8_t byte    = 0;
    const std::uint8_t* output = nullptr;
    std::size_t output_size    = 0;
    media::HFrameType main_type{};
    media::HFrameType lead_type{};
    CHECK_FALSE(config.ParseAndPrepare(&byte, 1, output, output_size, main_type, lead_type));

    config.SetParameters({1, 66, 0, 30, 0xff, 0xe1, 0, 20, 0x67});
    CHECK_FALSE(config.HasParameters());
    config.SetParameters(ValidAvcC());
    REQUIRE(config.HasParameters());
    config.SetParameters({1, 66, 0});
    CHECK(config.HasParameters());
}

TEST_CASE("RtmpCodecConfig parses AVC and HEVC parameter records atomically", "[rtmp][codec]") {
    SECTION("AVC") {
        RtmpCodecConfig config(media::VideoCodecType::kH264, 1280, 720, 25.0F);
        config.SetParameters(ValidAvcC());
        CHECK(config.HasParameters());
    }

    SECTION("HEVC") {
        RtmpCodecConfig config(media::VideoCodecType::kH265, 1280, 720, 25.0F);
        config.SetParameters(ValidHevcC());
        REQUIRE(config.HasParameters());

        AVFormatContext* context = avformat_alloc_context();
        REQUIRE(context != nullptr);
        AVStream* stream = avformat_new_stream(context, nullptr);
        REQUIRE(stream != nullptr);
        config.ConfigureStream(stream);
        CHECK(stream->codecpar->codec_id == AV_CODEC_ID_HEVC);
        CHECK(stream->codecpar->profile == FF_PROFILE_HEVC_MAIN);
        CHECK(stream->codecpar->extradata_size == 18);
        avformat_free_context(context);
    }
}

TEST_CASE("RtmpCodecConfig handles invalid rate and resolution arithmetic", "[rtmp][codec]") {
    CHECK(RtmpCodecConfig::EstimateBitrate(INT_MAX, INT_MAX) == 4000000);
    CHECK(RtmpCodecConfig::EstimateBitrate(-1, 1080) == 1000000);

    RtmpCodecConfig config(media::VideoCodecType::kH264, 1920, 1080, 0.0F);
    config.SetParameters(ValidAvcC());
    AVFormatContext* context = avformat_alloc_context();
    REQUIRE(context != nullptr);
    AVStream* stream = avformat_new_stream(context, nullptr);
    REQUIRE(stream != nullptr);
    config.ConfigureStream(stream);
    CHECK(stream->duration == 3600);
    CHECK(stream->r_frame_rate.num == 25000);
    CHECK(stream->r_frame_rate.den == 1000);
    avformat_free_context(context);
}

TEST_CASE("RtmpCodecConfig recognizes an Annex-B packet with parameter sets and slice", "[rtmp][codec]") {
    RtmpCodecConfig config(media::VideoCodecType::kH264, 640, 480, 25.0F);
    const std::vector<std::uint8_t> packet{0x00, 0x00, 0x00, 0x01, 0x67, 0x00, 0x00, 0x00,
                                           0x01, 0x68, 0x00, 0x00, 0x00, 0x01, 0x65, 0x88};
    const std::uint8_t* output = nullptr;
    std::size_t output_size    = 0;
    media::HFrameType main_type{};
    media::HFrameType lead_type{};
    REQUIRE(config.ParseAndPrepare(packet.data(), packet.size(), output, output_size, main_type, lead_type));
    CHECK(config.HasParameters());
    CHECK(main_type == media::HFrameType::I);
    CHECK(lead_type == media::HFrameType::SPS);
    CHECK(output == packet.data());
    CHECK(output_size == packet.size());
}

namespace {
    std::vector<std::vector<uint8_t>> ParseCanonicalAnnexB(const uint8_t* data, size_t size) {
        std::vector<std::vector<uint8_t>> nal_units;
        size_t offset = 0;
        while (offset < size) {
            REQUIRE(offset + 4 <= size);
            REQUIRE(data[offset] == 0x00);
            REQUIRE(data[offset + 1] == 0x00);
            REQUIRE(data[offset + 2] == 0x00);
            REQUIRE(data[offset + 3] == 0x01);
            const size_t nal_start = offset + 4;
            size_t next            = nal_start;
            while (next + 4 <= size && !(data[next] == 0x00 && data[next + 1] == 0x00 &&
                                         data[next + 2] == 0x00 && data[next + 3] == 0x01)) {
                ++next;
            }
            const size_t nal_end = next + 4 <= size ? next : size;
            REQUIRE(nal_end > nal_start);
            nal_units.emplace_back(data + nal_start, data + nal_end);
            offset = nal_end;
        }
        return nal_units;
    }
}  // namespace

TEST_CASE("RTMP codec config removes padded Annex-B separators", "[media][preview][rtmp]") {
    const std::vector<uint8_t> packet{
        0x00,
        0x00,
        0x00,
        0x01,
        0x67,
        0x4d,
        0x40,
        0x32,
        0x00,
        0x00,
        0x00,
        0x01,
        0x68,
        0xce,
        0x3c,
        0x80,
        // Legal trailing_zero_8bits followed by a four-byte start code. The legacy
        // FLV muxer previously serialized this run as an empty AVCC NAL.
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x01,
        0x06,
        0x05,
        0x2f,
        0x00,
        0x00,
        0x00,
        0x01,
        0x65,
        0x88,
        0x80,
        0x0e,
    };

    cosmo::RtmpCodecConfig config(cosmo::media::VideoCodecType::kH264, 1920, 1080, 24.0F);
    const uint8_t* output = nullptr;
    size_t output_size    = 0;
    cosmo::media::HFrameType main_type{};
    cosmo::media::HFrameType lead_type{};

    REQUIRE(config.ParseAndPrepare(packet.data(), packet.size(), output, output_size, main_type, lead_type));
    REQUIRE(main_type == cosmo::media::HFrameType::I);
    REQUIRE(lead_type == cosmo::media::HFrameType::SPS);

    const auto nal_units = ParseCanonicalAnnexB(output, output_size);
    REQUIRE(nal_units.size() == 4);
    REQUIRE((nal_units[0][0] & 0x1f) == 7);
    REQUIRE((nal_units[1][0] & 0x1f) == 8);
    REQUIRE((nal_units[2][0] & 0x1f) == 6);
    REQUIRE((nal_units[3][0] & 0x1f) == 5);
}

TEST_CASE("RTMP codec config canonicalizes ordinary H264 access units", "[media][preview][rtmp]") {
    const std::vector<uint8_t> packet{
        0x00, 0x00, 0x00, 0x01, 0x09, 0x30, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x02, 0x07,
    };

    cosmo::RtmpCodecConfig config(cosmo::media::VideoCodecType::kH264, 1280, 720, 25.0F);
    const uint8_t* output = nullptr;
    size_t output_size    = 0;
    cosmo::media::HFrameType main_type{};
    cosmo::media::HFrameType lead_type{};

    REQUIRE(config.ParseAndPrepare(packet.data(), packet.size(), output, output_size, main_type, lead_type));
    const auto nal_units = ParseCanonicalAnnexB(output, output_size);
    REQUIRE(nal_units.size() == 2);
    REQUIRE((nal_units[0][0] & 0x1f) == 9);
    REQUIRE((nal_units[1][0] & 0x1f) == 1);
}

TEST_CASE("RTMP codec config removes padded H265 Annex-B separators", "[media][preview][rtmp]") {
    const std::vector<uint8_t> packet{
        0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x42, 0x01,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x4e, 0x01, 0x80, 0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf,
    };

    cosmo::RtmpCodecConfig config(cosmo::media::VideoCodecType::kH265, 1920, 1080, 25.0F);
    const uint8_t* output = nullptr;
    size_t output_size    = 0;
    cosmo::media::HFrameType main_type{};
    cosmo::media::HFrameType lead_type{};

    REQUIRE(config.ParseAndPrepare(packet.data(), packet.size(), output, output_size, main_type, lead_type));
    REQUIRE(main_type == cosmo::media::HFrameType::I);
    REQUIRE(lead_type == cosmo::media::HFrameType::VPS);

    const auto nal_units = ParseCanonicalAnnexB(output, output_size);
    REQUIRE(nal_units.size() == 5);
    REQUIRE(((nal_units[0][0] & 0x7e) >> 1) == 32);
    REQUIRE(((nal_units[1][0] & 0x7e) >> 1) == 33);
    REQUIRE(((nal_units[2][0] & 0x7e) >> 1) == 34);
    REQUIRE(((nal_units[3][0] & 0x7e) >> 1) == 39);
    REQUIRE(((nal_units[4][0] & 0x7e) >> 1) == 19);
}

}  // namespace cosmo
