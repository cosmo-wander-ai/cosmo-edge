#include <deque>
#include <memory>
#include <vector>

#include "catch_amalgamated.hpp"
#include "flow/channel/AlgChannel.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockVideoFrameCodec.h"
#include "support/MockDefaults.h"
#include "support/ScopedServiceOverride.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

// Access the real channel handler without starting its background thread.
struct ChannelDecodeSequenceTest {
    static void Install(AlgChannelDecode& channel, std::unique_ptr<media::VideoDecoder> decoder) {
        channel.decoder_      = std::move(decoder);
        channel.stream_index_ = 0;
        channel.frame_index_  = -1;
    }
    static void Handle(AlgChannelDecode& channel, int64_t index, bool key,
                       media::VideoCodecType codec = media::VideoCodecType::kH264) {
        auto packet               = std::make_shared<media::VideoPacket>();
        packet->index             = index;
        packet->stream_idx        = 0;
        packet->width             = 640;
        packet->height            = 360;
        packet->fps               = 25;
        packet->codec_type        = codec;
        packet->is_i_frame        = key;
        packet->data              = {1};  // Use the explicit keyframe bit in this fake backend test.
        auto data                 = std::make_shared<AlgData>();
        data->dataType            = AlgDataType::ChannelDataOrig;
        data->chanDataOrig.packet = packet;
        data->firstTimePoint      = std::chrono::steady_clock::now();
        channel.HandFrame(data);
    }
    static int64_t Index(const AlgChannelDecode& channel) {
        return channel.frame_index_;
    }
};

namespace {
    class SequenceDecoder final : public media::VideoDecoder {
    public:
        SequenceDecoder() : VideoDecoder(0) {}
        bool Open() override {
            return true;
        }
        bool Close() override {
            return true;
        }
        bool IsOpened() override {
            return true;
        }
        bool SendPacket(const uint8_t*, size_t, int64_t index) override {
            submitted.push_back(index);
            current = index;
            return index != reject_index;
        }
        VideoFramePtr GetFrame() override {
            return nullptr;
        }
        media::DecodedVideoFrame GetDecodedFrame() override {
            if (current != fail_materialize_at)
                return {};
            const bool inactive = inactive_materializer;
            return media::DecodedVideoFrame(
                static_cast<uint64_t>(reordered_output_index), 640, 360, media::PixelFormat::PIXEL_I420,
                [inactive] {
                    if (inactive)
                        return std::make_shared<media::VideoFrame>(0, 0, media::PixelFormat::PIXEL_I420);
                    return VideoFramePtr{};
                },
                [] {});
        }
        std::vector<int64_t> submitted;
        int64_t current{-1};
        int64_t reject_index{-1};
        int64_t fail_materialize_at{-1};
        int64_t reordered_output_index{-1};
        bool inactive_materializer{false};
    };

    struct Fixture {
        test::MockAppInfoService app;
        test::ScopedServiceOverride<service::IAppInfoService> registration{app};
        test::NamedExpectations expectations;
        ActionNode action;
        std::unique_ptr<AlgChannel> owner;
        std::unique_ptr<AlgChannelDecode> decoder;
        AlgTaskUnit task;
        SequenceDecoder* fake{nullptr};

        Fixture() {
            expectations.push_back(NAMED_ALLOW_CALL(app, GetNumber()).RETURN(1));
            owner        = std::make_unique<AlgChannel>("decode-sequence", "task", action);
            decoder      = std::make_unique<AlgChannelDecode>(*owner, "decode-sequence-test");
            auto backend = std::make_unique<SequenceDecoder>();
            fake         = backend.get();
            ChannelDecodeSequenceTest::Install(*decoder, std::move(backend));
            task.channel_id   = "decode-sequence";
            task.task_id      = "task";
            task.actionId     = std::string(AADetect_Code);
            task.flowActionId = "detect";
            task.fps          = 10.0f;
            task.que          = std::make_shared<AlgDataQueue<AlgDataPtr>>("decode-sequence-test", 4);
            REQUIRE(decoder->RegistProcQueue(task));
        }
        ~Fixture() {
            if (decoder)
                decoder->RemoveProcQueue(task);
        }
    };
}  // namespace

TEST_CASE("A consumed packet advances after reordered output materialization fails",
          "[channel-decode][materialization-failure]") {
    Fixture f;
    f.fake->fail_materialize_at    = 2;
    f.fake->reordered_output_index = 1;
    ChannelDecodeSequenceTest::Handle(*f.decoder, 1, true);
    CHECK(ChannelDecodeSequenceTest::Index(*f.decoder) == 1);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 2, false);
    CHECK(f.decoder->GetStatus() == util::ErrorEnum::DecoderFrameFailed);
    CHECK(ChannelDecodeSequenceTest::Index(*f.decoder) == 2);
    CHECK(f.task.que->RestSize() == 0);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 3, false);
    CHECK(f.fake->submitted == std::vector<int64_t>{1, 2, 3});
    CHECK(ChannelDecodeSequenceTest::Index(*f.decoder) == 3);
}

TEST_CASE("A rejected decoder packet does not advance input sequence",
          "[channel-decode][materialization-failure]") {
    Fixture f;
    f.fake->reject_index = 2;
    ChannelDecodeSequenceTest::Handle(*f.decoder, 1, true);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 2, false);
    CHECK(f.decoder->GetStatus() == util::ErrorEnum::DecoderFrameFailed);
    CHECK(ChannelDecodeSequenceTest::Index(*f.decoder) == 1);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 3, false);
    CHECK(f.fake->submitted == std::vector<int64_t>{1, 2});
    ChannelDecodeSequenceTest::Handle(*f.decoder, 4, true);
    CHECK(f.fake->submitted == std::vector<int64_t>{1, 2, 4});
}

TEST_CASE("An inactive materialized video frame does not block the next packet",
          "[channel-decode][materialization-failure]") {
    Fixture f;
    f.fake->fail_materialize_at    = 2;
    f.fake->reordered_output_index = 2;
    f.fake->inactive_materializer  = true;
    ChannelDecodeSequenceTest::Handle(*f.decoder, 1, true);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 2, false);
    CHECK(f.decoder->GetStatus() == util::ErrorEnum::DecoderFrameFailed);
    CHECK(ChannelDecodeSequenceTest::Index(*f.decoder) == 2);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 3, false);
    CHECK(f.fake->submitted == std::vector<int64_t>{1, 2, 3});
}

TEST_CASE("Invalid MJPEG output retains its previous sequence behavior",
          "[channel-decode][materialization-failure]") {
    Fixture f;
    test::MockVideoFrameCodec codec;
    test::ScopedServiceOverride<service::IVideoFrameCodec> registration{codec};
    auto invalid     = std::make_shared<media::VideoFrame>(0, 0, media::PixelFormat::PIXEL_I420);
    auto expectation = NAMED_ALLOW_CALL(codec, DecodeJpeg(trompeloeil::_)).RETURN(invalid);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 1, true);
    ChannelDecodeSequenceTest::Handle(*f.decoder, 2, true, media::VideoCodecType::kMjpeg);
    CHECK(f.decoder->GetStatus() == util::ErrorEnum::DecoderFrameFailed);
    CHECK(ChannelDecodeSequenceTest::Index(*f.decoder) == 1);
}
}  // namespace cosmo
