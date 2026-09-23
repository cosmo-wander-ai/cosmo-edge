#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"

#define private public
#include "flow/channel/AlgChannel.h"
#include "flow/stream/StreamViewer.h"
#undef private

#include "mock/MockAppInfoService.h"
#include "mock/MockConfigReadService.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"
#include "util/VideoInfo.h"

namespace {

constexpr size_t kFixtureWidth  = 1280;
constexpr size_t kFixtureHeight = 720;

class ReferenceTrackingDecoder final : public cosmo::media::VideoDecoder {
public:
    ReferenceTrackingDecoder() : VideoDecoder(0) {}

    bool Open() override {
        return true;
    }
    bool Close() override {
        return true;
    }
    bool IsOpened() override {
        return true;
    }
    bool SendPacket(const uint8_t*, size_t, int64_t frame_index) override {
        received.push_back(frame_index);
        return true;
    }
    cosmo::media::VideoFramePtr GetFrame() override {
        return nullptr;
    }
    cosmo::media::DecodedVideoFrame GetDecodedFrame() override {
        return cosmo::media::DecodedVideoFrame(
            static_cast<uint64_t>(received.back()), kFixtureWidth, kFixtureHeight,
            cosmo::media::PixelFormat::PIXEL_I420,
            [this]() {
                ++materializations;
                return cosmo::media::VideoFramePtr{};
            },
            [this]() { ++discards; });
    }

    std::vector<int64_t> received;
    size_t materializations{0};
    size_t discards{0};
};

cosmo::AlgDataPtr PacketData(int64_t index, bool keyframe, int64_t generation = 1) {
    auto data                 = std::make_shared<cosmo::AlgData>();
    data->dataType            = cosmo::AlgDataType::ChannelDataOrig;
    auto packet               = std::make_shared<cosmo::media::VideoPacket>();
    packet->data              = {0, 0, 0, 1, static_cast<uint8_t>(keyframe ? 0x65 : 0x41), 0x80};
    packet->index             = index;
    packet->stream_idx        = generation;
    packet->codec_type        = cosmo::media::VideoCodecType::kH264;
    packet->width             = kFixtureWidth;
    packet->height            = kFixtureHeight;
    packet->fps               = 25;
    packet->is_i_frame        = keyframe;
    data->chanDataOrig.packet = packet;
    data->chanDataOrig.fps    = 25;
    return data;
}

}  // namespace

TEST_CASE("An idle running decoder keeps references without materializing unused output",
          "[preview-media][decoder]") {
    cosmo::test::MockAppInfoService app_info;
    cosmo::test::ScopedServiceOverride<cosmo::service::IAppInfoService> app_override{app_info};
    ALLOW_CALL(app_info, GetNumber()).RETURN(0);
    cosmo::ActionNode action;
    cosmo::AlgChannel channel("reference-handoff", "channel-task", action);
    auto& decode                   = channel.decoder_;
    auto tracker                   = std::make_unique<ReferenceTrackingDecoder>();
    auto* observed                 = tracker.get();
    decode.decoder_                = std::move(tracker);
    decode.stream_index_           = 1;
    decode.cap_image_stream_index_ = 1;

    // Exercise the decoding branch, not an unsupported-resolution rejection.
    auto first_packet = PacketData(1, true)->chanDataOrig.packet;
    REQUIRE(cosmo::media::IsValidVideoResolution(first_packet->GetWidth(), first_packet->GetHeight()));
    REQUIRE(first_packet->IsIFrame());
    REQUIRE(decode.ValidateFrame(first_packet, false));

    // More than the old raw viewer queue's 300-packet capacity. No historical
    // packet cache is needed: decoder input remains contiguous with no viewer.
    for (int64_t index = 1; index <= 521; ++index) {
        decode.HandFrame(PacketData(index, index == 1));
    }
    REQUIRE(observed->received.size() == 521);
    CHECK(observed->received.front() == 1);
    CHECK(observed->received.back() == 521);
    CHECK(decode.frame_index_ == 521);
    CHECK(observed->materializations == 0);
    CHECK(observed->discards == 521);

    // An incoming current P frame is valid immediately; there is no need for
    // an old-IDR replay or a future IDR to repair an I-only input gap.
    auto next_packet = PacketData(522, false)->chanDataOrig.packet;
    CHECK(decode.ValidateFrame(next_packet, false));
    auto missing_reference = PacketData(523, false)->chanDataOrig.packet;
    CHECK_FALSE(decode.ValidateFrame(missing_reference, false));
}

TEST_CASE("A late passthrough subscriber receives no old cached IDR", "[preview-media][demux]") {
    cosmo::test::MockConfigReadService config;
    cosmo::test::ScopedServiceOverride<cosmo::service::IConfigReadService> config_override{config};
    ALLOW_CALL(config, IsNetworkModel()).RETURN(false);

    // Same independently decodable 16x16 H.264 frame as the RTMP startup test.
    const std::vector<uint8_t> keyframe{
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7b, 0x01, 0x10, 0x00, 0x00, 0x03, 0x00,
        0x10, 0x00, 0x00, 0x03, 0x00, 0x28, 0xf1, 0x22, 0x6a, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x0f,
        0xc8, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09, 0x02, 0xe0,
    };
    const auto path = std::filesystem::temp_directory_path() /
                      ("cosmo-preview-keyframe-" + cosmo::util::GenerateUUID() + ".h264");
    struct RemoveFile {
        std::filesystem::path path;
        ~RemoveFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    {
        std::ofstream stream(path, std::ios::binary);
        for (int i = 0; i < 3; ++i) {
            stream.write(reinterpret_cast<const char*>(keyframe.data()),
                         static_cast<std::streamsize>(keyframe.size()));
        }
        REQUIRE(stream.good());
    }

    cosmo::AlgChannelDemux demux("late-passthrough", path.string());
    demux.SetVideoFps(1000);
    REQUIRE(demux.OpenStream());
    // No recording is requested. Keep recorder service dependencies outside
    // this media subscription regression.
    demux.recorder_.reset();
    demux.HandleStream();
    REQUIRE(demux.GetLastFrame());
    REQUIRE(demux.GetLastFrame()->IsIFrame());
    REQUIRE(demux.GetLastFrame()->index == 1);

    auto queue = std::make_shared<cosmo::AsyncQueue<VideoPacketPtr>>("late-preview", 300);
    demux.AddViewerPacketQueue(queue);
    cosmo::util::AsyncQueueInfo status;
    REQUIRE(queue->Status(status));
    CHECK(status.status.insertCount == 0);

    demux.HandleStream();
    REQUIRE(queue->Status(status));
    CHECK(status.status.insertCount == 1);
    CHECK(demux.GetLastFrame()->index == 2);
    demux.RemoveViewerPacketQueue(queue);
    demux.CloseStream();
}

TEST_CASE("Concurrent viewer Stop waits until callback detachment completes", "[preview-media][lifecycle]") {
    cosmo::test::MockAppInfoService app_info;
    cosmo::test::ScopedServiceOverride<cosmo::service::IAppInfoService> app_override{app_info};
    ALLOW_CALL(app_info, GetNumber()).RETURN(0);
    cosmo::ActionNode action;
    auto channel = std::make_shared<cosmo::AlgChannel>("stop-handoff", "channel-task", action);
    // Offline construction creates queues without opening a publisher.
    cosmo::StreamViewer viewer(channel, "stop-handoff", "");
    std::mutex mutex;
    std::condition_variable changed;
    bool callback_entered = false;
    bool release_callback = false;
    viewer.async_packet_queue_->SetProcessor([&](VideoPacketPtr&&) {
        std::unique_lock<std::mutex> lock(mutex);
        callback_entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return release_callback; });
    });
    REQUIRE(viewer.async_packet_queue_->Insert(std::make_shared<cosmo::media::VideoPacket>()));
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return callback_entered; });
    }
    auto first = std::async(std::launch::async, [&] { viewer.Stop(); });
    while (!viewer.IsStopped()) {
        std::this_thread::yield();
    }
    std::promise<void> second_entered;
    auto entered = second_entered.get_future();
    auto second  = std::async(std::launch::async, [&] {
        second_entered.set_value();
        viewer.Stop();
    });
    entered.wait();
    const bool second_waited = second.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_callback = true;
    }
    changed.notify_all();
    first.get();
    second.get();
    CHECK(second_waited);
}
