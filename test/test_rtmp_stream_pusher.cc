#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

extern "C" {
#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
}

#include "catch_amalgamated.hpp"
#include "flow/stream/RtmpStreamPusher.h"
#include "util/UuidUtil.h"

namespace {
// The same decodable 16x16 black access unit used by the existing startup
// regression below, split into out-of-band SPS/PPS and an unchanged IDR.
const std::vector<uint8_t> kRawHeaders{0,    0,    0,    1, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7b, 1,
                                       0x10, 0,    0,    3, 0,    0x10, 0,    0,    3,    0,    0x28,
                                       0xf1, 0x22, 0x6a, 0, 0,    0,    1,    0x68, 0xce, 0x0f, 0xc8};
const std::vector<uint8_t> kRawIdr{0, 0, 1, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0, 9, 2, 0xe0};
const std::vector<uint8_t> kSkippedP{0, 0, 1, 0x41, 0x9a, 0x80};

struct TempFlv {
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("cosmo-raw-preview-" + cosmo::util::GenerateUUID() + ".flv");
    ~TempFlv() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

cosmo::media::VideoPacket RawPacket(int64_t generation, const std::vector<uint8_t>& headers,
                                    const std::vector<uint8_t>& payload) {
    cosmo::media::VideoPacket packet;
    packet.stream_idx      = generation;
    packet.codec_type      = cosmo::media::VideoCodecType::kH264;
    packet.width           = 640;
    packet.height          = 480;
    packet.fps             = 25;
    packet.codec_extradata = std::make_shared<const std::vector<uint8_t>>(headers);
    packet.data            = payload;
    return packet;
}

void CheckDecodableRawFlv(const std::filesystem::path& path, uint8_t expected_level) {
    AVFormatContext* input = nullptr;
    REQUIRE(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
    const auto close_input = [](AVFormatContext* ctx) { avformat_close_input(&ctx); };
    std::unique_ptr<AVFormatContext, decltype(close_input)> input_owner(input, close_input);
    REQUIRE(avformat_find_stream_info(input, nullptr) >= 0);
    const int stream_index = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(stream_index >= 0);
    const auto* parameters = input->streams[stream_index]->codecpar;
    REQUIRE(parameters->extradata_size > 4);
    CHECK(parameters->extradata[0] == 1);  // FLV sequence header contains avcC.
    CHECK(parameters->extradata[3] == expected_level);

    const auto* decoder = avcodec_find_decoder_by_name("h264");
    REQUIRE(decoder != nullptr);
    const auto free_decoder = [](AVCodecContext* ctx) { avcodec_free_context(&ctx); };
    std::unique_ptr<AVCodecContext, decltype(free_decoder)> context(avcodec_alloc_context3(decoder),
                                                                    free_decoder);
    REQUIRE(context != nullptr);
    REQUIRE(avcodec_parameters_to_context(context.get(), parameters) == 0);
    context->thread_count = 1;
    REQUIRE(avcodec_open2(context.get(), decoder, nullptr) == 0);
    const auto free_packet = [](AVPacket* packet) { av_packet_free(&packet); };
    std::unique_ptr<AVPacket, decltype(free_packet)> packet(av_packet_alloc(), free_packet);
    const auto free_frame = [](AVFrame* frame) { av_frame_free(&frame); };
    std::unique_ptr<AVFrame, decltype(free_frame)> frame(av_frame_alloc(), free_frame);
    REQUIRE(packet != nullptr);
    REQUIRE(frame != nullptr);
    int frames       = 0;
    const auto drain = [&] {
        int result = 0;
        while ((result = avcodec_receive_frame(context.get(), frame.get())) == 0) {
            CHECK(frame->width == 16);
            CHECK(frame->height == 16);
            ++frames;
            av_frame_unref(frame.get());
        }
        CHECK((result == AVERROR(EAGAIN) || result == AVERROR_EOF));
    };
    int read_result = 0;
    while ((read_result = av_read_frame(input, packet.get())) == 0) {
        if (packet->stream_index == stream_index) {
            REQUIRE(avcodec_send_packet(context.get(), packet.get()) == 0);
            drain();
        }
        av_packet_unref(packet.get());
    }
    CHECK(read_result == AVERROR_EOF);
    REQUIRE(avcodec_send_packet(context.get(), nullptr) == 0);
    drain();
    CHECK(frames == 1);
}
}  // namespace

TEST_CASE("Raw RTMP preview seeds SDP headers and waits through initial P frames",
          "[rtmp][regression][codec-headers]") {
    const bool avcc = GENERATE(false, true);
    auto headers    = kRawHeaders;
    if (avcc) {
        headers = {1, 0x42, 0xc0, 0x0a, 0xff, 0xe1, 0, 21};
        headers.insert(headers.end(), kRawHeaders.begin() + 4, kRawHeaders.begin() + 25);
        headers.insert(headers.end(), {1, 0, 4});
        headers.insert(headers.end(), kRawHeaders.begin() + 29, kRawHeaders.end());
    }
    TempFlv output;
    cosmo::RtmpStreamPusher pusher(cosmo::media::VideoCodecType::kH264, output.path.string(), 640, 480, 25);
    const auto p_frame = RawPacket(1, headers, kSkippedP);
    pusher.PushPacket(p_frame);
    pusher.PushPacket(p_frame);
    CHECK(pusher.GetProcInfo().recvFrames == 2);
    CHECK(pusher.GetProcInfo().sendFrames == 0);
    CHECK_FALSE(pusher.IsReady());
    const auto idr = RawPacket(1, headers, kRawIdr);
    pusher.PushPacket(idr);
    CHECK(pusher.WaitReady(std::chrono::milliseconds(100)));
    CHECK(pusher.GetProcInfo().recvFrames == 3);
    CHECK(pusher.GetProcInfo().sendFrames == 1);
    CHECK(idr.data == kRawIdr);
    CHECK(*idr.codec_extradata == headers);
    pusher.Stop();
    CheckDecodableRawFlv(output.path, 0x0a);
}

TEST_CASE("Raw FLV preserves presentation offsets with monotonically increasing decode time",
          "[rtmp][packet-time]") {
    TempFlv output;
    {
        cosmo::RtmpStreamPusher pusher(cosmo::media::VideoCodecType::kH264, output.path.string(), 640, 480,
                                       25);
        const std::array<int64_t, 3> dts{0, 3600, 7200};
        const std::array<int64_t, 3> pts{7200, 14400, 7200};
        for (size_t i = 0; i < dts.size(); ++i) {
            auto packet          = RawPacket(1, kRawHeaders, kRawIdr);
            packet.pts           = pts[i];
            packet.dts           = dts[i];
            packet.duration      = 3600;
            packet.time_base_num = 1;
            packet.time_base_den = 90000;
            pusher.PushPacket(packet);
        }
        CHECK(pusher.GetProcInfo().sendFrames == 3);
        pusher.Stop();
    }
    AVFormatContext* input = nullptr;
    REQUIRE(avformat_open_input(&input, output.path.c_str(), nullptr, nullptr) == 0);
    const auto close_input = [](AVFormatContext* ctx) { avformat_close_input(&ctx); };
    std::unique_ptr<AVFormatContext, decltype(close_input)> owner(input, close_input);
    REQUIRE(avformat_find_stream_info(input, nullptr) >= 0);
    const int stream_index = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(stream_index >= 0);
    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    std::vector<int64_t> seen_dts, seen_pts;
    while (av_read_frame(input, packet) == 0) {
        if (packet->stream_index == stream_index) {
            const auto tb = input->streams[stream_index]->time_base;
            seen_dts.push_back(av_rescale_q(packet->dts, tb, AVRational{1, 1000}));
            seen_pts.push_back(av_rescale_q(packet->pts, tb, AVRational{1, 1000}));
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    CHECK((seen_dts == std::vector<int64_t>{0, 40, 80}));
    CHECK((seen_pts == std::vector<int64_t>{80, 160, 80}));
}

TEST_CASE("Raw RTMP preview replaces codec parameters after source reconnect",
          "[rtmp][regression][codec-headers]") {
    TempFlv output;
    cosmo::RtmpStreamPusher pusher(cosmo::media::VideoCodecType::kH264, output.path.string(), 640, 480, 25);
    const auto first = RawPacket(1, kRawHeaders, kRawIdr);
    pusher.PushPacket(first);
    REQUIRE(pusher.IsReady());
    auto updated_headers = kRawHeaders;
    updated_headers[7]   = 0x0b;  // New generation advertises a different H.264 level.
    int64_t generation   = 2;
    SECTION("complete new SDP parameters") {}
    SECTION("absent new parameters must not reuse the old SPS and PPS") {
        auto missing = RawPacket(generation++, {}, kRawIdr);
        missing.codec_extradata.reset();
        pusher.PushPacket(missing);
        CHECK_FALSE(pusher.IsReady());
        CHECK(pusher.GetProcInfo().sendFrames == 1);
    }
    SECTION("malformed new parameters must not reuse the old SPS and PPS") {
        const auto malformed = RawPacket(generation++, {0, 0, 1, 0x67, 0x42}, kRawIdr);
        pusher.PushPacket(malformed);
        CHECK_FALSE(pusher.IsReady());
        CHECK(pusher.GetProcInfo().sendFrames == 1);
    }
    const auto p_frame = RawPacket(generation, updated_headers, kSkippedP);
    pusher.PushPacket(p_frame);
    pusher.PushPacket(p_frame);
    CHECK_FALSE(pusher.IsReady());
    CHECK(pusher.GetProcInfo().sendFrames == 1);
    const auto second = RawPacket(generation, updated_headers, kRawIdr);
    pusher.PushPacket(second);
    CHECK(pusher.IsReady());
    CHECK(pusher.GetProcInfo().sendFrames == 2);
    CHECK(first.data == kRawIdr);
    CHECK(*first.codec_extradata == kRawHeaders);
    CHECK(second.data == kRawIdr);
    CHECK(*second.codec_extradata == updated_headers);
    pusher.Stop();
    // Reopening this file transport replaces it with the new FLV generation.
    // Verify its actual sequence header and decode its IDR using FFmpeg, rather
    // than treating only a successful write counter as media acceptance.
    CheckDecodableRawFlv(output.path, 0x0b);
}

TEST_CASE("RtmpStreamPusher rejects empty frames concurrently", "[rtmp][concurrency]") {
    const auto output_path =
        std::filesystem::path("/tmp") / ("cosmo-rtmp-pusher-" + cosmo::util::GenerateUUID() + ".flv");

    {
        cosmo::RtmpStreamPusher pusher(cosmo::media::VideoCodecType::kH264, output_path.string(), 640, 480,
                                       25.0F);
        uint8_t byte = 0;
        std::vector<std::thread> producers;
        for (int i = 0; i < 4; ++i) {
            producers.emplace_back([&pusher, &byte]() {
                for (int frame = 0; frame < 5; ++frame) {
                    pusher.PushFrame(nullptr, 1);
                    pusher.PushFrame(&byte, 0);
                }
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }

        REQUIRE_FALSE(pusher.WaitReady(std::chrono::milliseconds(1)));
        REQUIRE(pusher.GetProcInfo().recvFrames == 0);
    }

    std::error_code error;
    std::filesystem::remove(output_path, error);
}

TEST_CASE("RtmpStreamPusher accepts a valid small startup keyframe", "[rtmp][regression]") {
    const auto [width, height, fps, expected_width, expected_height, expected_fps] =
        GENERATE(table<int, int, float, double, double, double>({{640, 480, 25.0F, 640, 480, 25},
                                                                 {1920, 1088, 30.0F, 1920, 1088, 30},
                                                                 {1921, 720, 30.0F, 1920, 1080, 30},
                                                                 {1280, 1089, 30.0F, 1920, 1080, 30},
                                                                 {16, 16, 1.0F, 1920, 1080, 1},
                                                                 {640, 480, 0.0F, 1920, 1080, 25},
                                                                 {640, 480, 0.05F, 1920, 1080, 0.05}}));
    CAPTURE(width, height, fps);
    const auto output_path =
        std::filesystem::path("/tmp") / ("cosmo-rtmp-small-keyframe-" + cosmo::util::GenerateUUID() + ".flv");

    // Decodable Annex-B H.264 access unit generated from one 16x16 black frame
    // (SPS, PPS and IDR). Small keyframes are valid for low-resolution or
    // low-complexity scenes and must not be rejected by an arbitrary threshold.
    const std::vector<std::uint8_t> small_keyframe{
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7b, 0x01, 0x10, 0x00, 0x00, 0x03, 0x00,
        0x10, 0x00, 0x00, 0x03, 0x00, 0x28, 0xf1, 0x22, 0x6a, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x0f,
        0xc8, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09, 0x02, 0xe0,
    };
    REQUIRE(small_keyframe.size() < 1024);

    {
        cosmo::RtmpStreamPusher pusher(cosmo::media::VideoCodecType::kH264, output_path.string(), width,
                                       height, fps);
        SECTION("constructor attributes") {}
        SECTION("attribute updates preserve the established codec configuration") {
            pusher.SetVideoAttr(1280, 720, 60.0F);
            pusher.SetVideoAttr(0, 0, 0.0F);
        }
        pusher.PushFrame(small_keyframe.data(), small_keyframe.size());

        REQUIRE(pusher.WaitReady(std::chrono::milliseconds(100)));
        CHECK(pusher.GetProcInfo().recvFrames == 1);
        CHECK(pusher.GetProcInfo().sendFrames == 1);

        pusher.Stop();
        pusher.Stop();
        pusher.PushFrame(small_keyframe.data(), small_keyframe.size());
        CHECK_FALSE(pusher.IsReady());
        CHECK_FALSE(pusher.WaitReady(std::chrono::milliseconds(1)));
        CHECK(pusher.GetProcInfo().recvFrames == 1);
        CHECK(pusher.GetProcInfo().sendFrames == 1);
    }

    REQUIRE(std::filesystem::exists(output_path));
    CHECK(std::filesystem::file_size(output_path) > 0);
    std::ifstream input(output_path, std::ios::binary);
    const std::string output{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto check_metadata = [&](const std::string& name, double value) {
        // Check AMF number entries in the emitted FLV header, before a decoder
        // can replace its dimensions with the tiny frame's SPS dimensions.
        std::string entry(2, '\0');
        AV_WB16(entry.data(), name.size());
        entry += name;
        entry.push_back('\0');
        uint8_t number[8];
        AV_WB64(number, av_double2int(value));
        entry.append(reinterpret_cast<const char*>(number), sizeof(number));
        INFO("FLV metadata: " << name << "=" << value);
        CHECK(output.find(entry) != std::string::npos);
    };
    check_metadata("width", expected_width);
    check_metadata("height", expected_height);
    check_metadata("framerate", expected_fps);
    std::error_code error;
    std::filesystem::remove(output_path, error);
}
