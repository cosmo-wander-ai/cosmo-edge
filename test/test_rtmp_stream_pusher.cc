#include <chrono>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/stream/RtmpStreamPusher.h"
#include "util/UuidUtil.h"

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
    const auto output_path =
        std::filesystem::path("/tmp") / ("cosmo-rtmp-small-keyframe-" + cosmo::util::GenerateUUID() + ".flv");

    // Decodable Annex-B H.264 access unit generated from one 16x16 black frame
    // (SPS, PPS and IDR). Small keyframes are valid for low-resolution or
    // low-complexity scenes and must not be rejected by an arbitrary threshold.
    const std::vector<std::uint8_t> small_keyframe{
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7b, 0x01, 0x10,
        0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x28, 0xf1, 0x22,
        0x6a, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x0f, 0xc8, 0x00, 0x00, 0x01,
        0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09, 0x02, 0xe0,
    };
    REQUIRE(small_keyframe.size() < 1024);

    {
        cosmo::RtmpStreamPusher pusher(cosmo::media::VideoCodecType::kH264, output_path.string(), 16, 16,
                                       1.0F);
        pusher.PushFrame(small_keyframe.data(), small_keyframe.size());

        REQUIRE(pusher.WaitReady(std::chrono::milliseconds(100)));
        CHECK(pusher.GetProcInfo().recvFrames == 1);
        CHECK(pusher.GetProcInfo().sendFrames == 1);
    }

    REQUIRE(std::filesystem::exists(output_path));
    CHECK(std::filesystem::file_size(output_path) > 0);
    std::error_code error;
    std::filesystem::remove(output_path, error);
}
