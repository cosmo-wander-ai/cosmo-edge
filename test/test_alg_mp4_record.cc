#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>

extern "C" {
#include "libavcodec/bsf.h"
#include "libavformat/avformat.h"
}

#include "catch_amalgamated.hpp"
#include "flow/channel/AlgChannelMp4.h"
#include "flow/channel/AlgMp4Record.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockTaskService.h"
#include "mp4v2/mp4v2.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/UuidUtil.h"

static_assert(!std::is_copy_constructible_v<cosmo::AlgMp4Record>);
static_assert(!std::is_copy_assignable_v<cosmo::AlgMp4Record>);
static_assert(!std::is_move_constructible_v<cosmo::AlgMp4Record>);
static_assert(!std::is_move_assignable_v<cosmo::AlgMp4Record>);

namespace {
using Bytes  = std::vector<uint8_t>;
using Codec  = cosmo::media::VideoCodecType;
namespace fs = std::filesystem;

enum class Fault { None, False, Exception, NoSample };
struct WriterState {
    std::mutex mutex;
    std::condition_variable changed;
    Fault fault{Fault::None};
    std::vector<const uint8_t*> addresses;
};
WriterState writer_state;
struct ScopedWriter {
    ScopedWriter() {
        std::lock_guard<std::mutex> lock(writer_state.mutex);
        writer_state.addresses.clear();
        writer_state.fault = Fault::None;
    }
    ~ScopedWriter() {
        SetFault(Fault::None);
    }
    bool WaitForCalls(size_t count) {
        std::unique_lock<std::mutex> lock(writer_state.mutex);
        return writer_state.changed.wait_for(lock, std::chrono::seconds(5),
                                             [count] { return writer_state.addresses.size() >= count; });
    }
    void SetFault(Fault fault) {
        std::lock_guard<std::mutex> lock(writer_state.mutex);
        writer_state.fault = fault;
    }
    std::vector<const uint8_t*> Addresses() {
        std::lock_guard<std::mutex> lock(writer_state.mutex);
        return writer_state.addresses;
    }
};

// H.264 parameters from test_rtmp_stream_pusher.cc; complete HEVC parameters
// from the vendored SRS kernel test's vps_sps_pps fixture. Slice payloads below
// test MP4 framing only; device acceptance separately validates actual decoding.
std::vector<Bytes> Parameters(Codec codec) {
    if (codec == Codec::kH264) {
        return {{0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7b, 0x01, 0x10, 0,    0,   3,
                 0,    0x10, 0,    0,    3,    0,    0x28, 0xf1, 0x22, 0x6a},
                {0x68, 0xce, 0x0f, 0xc8}};
    }
    return {{0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0,    0,    3,    0,
             0x90, 0,    0,    3,    0,    0,    3,    0,    0x5d, 0x95, 0x98, 0x09},
            {0x42, 0x01, 0x01, 0x01, 0x60, 0,    0,    3,    0,    0x90, 0,    0,    3,    0,
             0,    3,    0,    0x5d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16, 0x59, 0x59, 0xa4, 0x93,
             0x2b, 0xc0, 0x40, 0x40, 0,    0,    0xfa, 0x40, 0,    0x17, 0x70, 0x02},
            {0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40}};
}
Bytes Nalu(Codec codec, bool sync, size_t length = 16, uint8_t fill = 0xaa) {
    Bytes data(length, fill);
    data[0] = codec == Codec::kH264 ? (sync ? 0x65 : 0x41) : (sync ? 0x26 : 0x02);
    if (codec == Codec::kH265)
        data[1] = 1;
    return data;
}
Bytes Annex(const std::vector<Bytes>& nalus, bool short_first = false) {
    Bytes result;
    for (size_t i = 0; i < nalus.size(); ++i) {
        if ((i % 2 == 0) != short_first)
            result.push_back(0);
        result.insert(result.end(), {0, 0, 1});
        result.insert(result.end(), nalus[i].begin(), nalus[i].end());
    }
    return result;
}
Bytes Expected(const std::vector<Bytes>& nalus) {
    Bytes result;
    for (const auto& nalu : nalus) {
        const auto size = static_cast<uint32_t>(nalu.size());
        for (int shift : {24, 16, 8, 0})
            result.push_back(static_cast<uint8_t>(size >> shift));
        result.insert(result.end(), nalu.begin(), nalu.end());
    }
    return result;
}
VideoPacketPtr Packet(Bytes bytes, Codec codec, int64_t seq) {
    auto packet             = std::make_shared<cosmo::media::VideoPacket>();
    packet->data            = std::move(bytes);
    packet->codec_type      = codec;
    packet->stream_idx      = 0;
    packet->is_i_frame      = false;
    packet->index           = seq;
    packet->timestamp       = seq * 137;
    packet->timestamp_epoch = 1700000000000 + seq * 137;
    return packet;
}
struct Environment {
    std::string root = "/tmp/cosmo-mp4-" + cosmo::util::GenerateUUID();
    cosmo::test::ScopedPathOverride paths{root, root};
    cosmo::test::MockConfigReadService config;
    cosmo::test::MockTaskService tasks;
    cosmo::test::ScopedServiceOverride<cosmo::service::IConfigReadService> config_service{config};
    cosmo::test::ScopedServiceOverride<cosmo::service::ITaskQuery> task_service{tasks};
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;
    ScopedWriter writer;
    Environment() {
        expectations.push_back(NAMED_ALLOW_CALL(config, IsNetworkModel()).RETURN(false));
        expectations.push_back(
            NAMED_ALLOW_CALL(tasks, GetTaskDetHistory(trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                                      trompeloeil::_, trompeloeil::_))
                .RETURN(std::vector<cosmo::DataDetTrackClassify>{}));
        expectations.push_back(
            NAMED_ALLOW_CALL(tasks, GetTaskLiveOverviewInfo(trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                                            trompeloeil::_))
                .RETURN(std::vector<cosmo::MsgOverviewMem>{}));
    }
    ~Environment() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    cosmo::RecordParam Param(const std::string& id = "event") {
        cosmo::RecordParam p;
        p.recordId            = id;
        p.channelId           = "channel";
        p.taskId              = "task";
        p.streamIndex         = 0;
        p.frameTimestamp      = 1700000000000;
        p.startframeTimestamp = p.frameTimestamp;
        p.frameSeq            = 0;
        p.startFrameSeq       = 0;
        p.jsonPath = (fs::path(cosmo::path::GetEventPath(p.frameTimestamp)) / (id + ".json")).string();
        cosmo::MsgAlarmVideoOverviewInfo overview{};
        std::string json;
        REQUIRE(cosmo::util::EncodeJson(overview, json));
        cosmo::util::WriteFile(p.jsonPath, json);
        return p;
    }
    fs::path File(const std::string& suffix, const std::string& id = "event") {
        return fs::path(cosmo::path::GetEventPath(1700000000000)) / (id + suffix);
    }
};
struct Reader {
    MP4FileHandle handle;
    MP4TrackId track;
    explicit Reader(const fs::path& file) : handle(MP4Read(file.c_str())) {
        REQUIRE(handle != MP4_INVALID_FILE_HANDLE);
        REQUIRE(MP4GetNumberOfTracks(handle, MP4_VIDEO_TRACK_TYPE) == 1);
        track = MP4FindTrackId(handle, 0, MP4_VIDEO_TRACK_TYPE);
        REQUIRE(track != MP4_INVALID_TRACK_ID);
    }
    ~Reader() {
        MP4Close(handle);
    }
    void CheckParameters(Codec codec) {
        struct Headers {
            uint8_t** bytes{};
            uint32_t* sizes{};
            ~Headers() {
                if (bytes && sizes)
                    for (size_t i = 0; sizes[i]; ++i)
                        std::free(bytes[i]);
                std::free(bytes);
                std::free(sizes);
            }
            void Check(const Bytes& expected) {
                REQUIRE(bytes != nullptr);
                REQUIRE(sizes != nullptr);
                REQUIRE(sizes[0] == expected.size());
                CHECK(Bytes(bytes[0], bytes[0] + sizes[0]) == expected);
                CHECK(sizes[1] == 0);
            }
        } vps, sps, pps;
        auto parameters = Parameters(codec);
        uint32_t length{};
        if (codec == Codec::kH264) {
            REQUIRE(
                MP4GetTrackH264SeqPictHeaders(handle, track, &sps.bytes, &sps.sizes, &pps.bytes, &pps.sizes));
            REQUIRE(MP4GetTrackH264LengthSize(handle, track, &length));
            sps.Check(parameters[0]);
            pps.Check(parameters[1]);
        } else {
            REQUIRE(MP4GetTrackH265SeqPictHeaders(handle, track, &vps.bytes, &vps.sizes, &sps.bytes,
                                                  &sps.sizes, &pps.bytes, &pps.sizes));
            REQUIRE(MP4GetTrackH265LengthSize(handle, track, &length));
            // This vendored getter exposes the full hvcC flags byte plus one.
            // Only the low two bits encode lengthSizeMinusOne.
            length = ((length - 1) & 3) + 1;
            vps.Check(parameters[0]);
            sps.Check(parameters[1]);
            pps.Check(parameters[2]);
        }
        CHECK(length == 4);
        CHECK(MP4GetTrackTimeScale(handle, track) == 90000);
    }
    void Check(size_t index, const Bytes& expected, bool sync) {
        Bytes bytes(MP4GetSampleSize(handle, track, index));
        uint8_t* ptr  = bytes.data();
        uint32_t size = static_cast<uint32_t>(bytes.size());
        MP4Timestamp start{};
        MP4Duration duration{}, offset{};
        // The vendored IsSyncSample underflows its search bound for an empty
        // stss. Read sample bytes normally and inspect the real table directly.
        REQUIRE(MP4ReadSample(handle, track, index, &ptr, &size, &start, &duration, &offset, nullptr));
        bool actual_sync = !MP4HaveTrackAtom(handle, track, "mdia.minf.stbl.stss");
        if (!actual_sync) {
            uint64_t count{};
            REQUIRE(MP4GetTrackIntegerProperty(handle, track, "mdia.minf.stbl.stss.entryCount", &count));
            for (uint64_t i = 0; i < count; ++i) {
                const auto property = "mdia.minf.stbl.stss.entries[" + std::to_string(i) + "].sampleNumber";
                uint64_t sample{};
                REQUIRE(MP4GetTrackIntegerProperty(handle, track, property.c_str(), &sample));
                actual_sync = actual_sync || sample == index;
            }
        }
        bytes.resize(size);
        CHECK(bytes == expected);
        CHECK(actual_sync == sync);
        CHECK(duration == 3600);
        CHECK(offset == 0);
        CHECK(start == (index - 1) * 3600);
    }
};
void Configure(cosmo::AlgMp4Record& record, Codec codec) {
    REQUIRE_FALSE(record.RecodeFrame(Packet(Annex(Parameters(codec)), codec, 0)));
    REQUIRE(record.GetRecordFrames() == 0);
    REQUIRE(record.GetLastIndex() == 0);
}
}  // namespace

extern "C" bool __real_MP4WriteSample(MP4FileHandle, MP4TrackId, const uint8_t*, uint32_t, MP4Duration,
                                      MP4Duration, bool);
extern "C" bool __wrap_MP4WriteSample(MP4FileHandle file, MP4TrackId track, const uint8_t* bytes,
                                      uint32_t size, MP4Duration duration, MP4Duration offset, bool sync) {
    Fault fault;
    {
        std::lock_guard<std::mutex> lock(writer_state.mutex);
        writer_state.addresses.push_back(bytes);
        fault = writer_state.fault;
        writer_state.changed.notify_all();
    }
    if (fault == Fault::False)
        return false;
    if (fault == Fault::Exception)
        throw std::runtime_error("injected MP4 write failure");
    if (fault == Fault::NoSample)
        return true;
    return __real_MP4WriteSample(file, track, bytes, size, duration, offset, sync);
}

TEST_CASE("MP4 recording reuses one buffer and preserves long short long samples", "[mp4-record]") {
    auto codec = GENERATE(Codec::kH264, Codec::kH265);
    Environment env;
    std::vector<Bytes> nalus{Nalu(codec, true, 256, 0xab), Nalu(codec, false, 16, 0xcd),
                             Nalu(codec, true, 320, 0xef)};
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        Configure(record, codec);
        for (size_t i = 0; i < nalus.size(); ++i)
            REQUIRE(record.RecodeFrame(Packet(Annex({nalus[i]}), codec, i + 1)));
        REQUIRE(record.GetRecordFrames() == 3);
        REQUIRE(record.GetLastIndex() == 3);
        REQUIRE(record.RecodeFrame(Packet(Annex({nalus[1]}), codec, 4)));
        auto addresses = env.writer.Addresses();
        REQUIRE(addresses.size() == 4);
        CHECK(addresses[0] == addresses[1]);
        CHECK(addresses[2] == addresses[3]);
    }
    REQUIRE_FALSE(fs::exists(env.File("_video.tmp")));
    Reader reader(env.File("_video.mp4"));
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 4);
    for (size_t i = 0; i < nalus.size(); ++i)
        reader.Check(i + 1, Expected({nalus[i]}), i != 1);
    reader.Check(4, Expected({nalus[1]}), false);
}

TEST_CASE("MP4 recording frames every NALU and strips parameters at every position", "[mp4-record]") {
    auto codec    = GENERATE(Codec::kH264, Codec::kH265);
    auto position = GENERATE(0, 1, 2);
    Environment env;
    auto parameters = Parameters(codec);
    auto first      = Nalu(codec, false, 20);
    auto second     = Nalu(codec, false, 30, 0xbb);
    Bytes aud       = codec == Codec::kH264 ? Bytes{0x09, 0xf0} : Bytes{0x46, 0x01, 0x50};
    Bytes sei       = codec == Codec::kH264 ? Bytes{0x06, 0x05, 0x80} : Bytes{0x4e, 0x01, 0x05, 0x80};
    std::vector<Bytes> input{first, second};
    input.insert(input.begin() + position, parameters.begin(), parameters.end());
    input.insert(input.begin(), aud);
    input.push_back(sei);
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        REQUIRE(record.RecodeFrame(Packet(Annex(input, true), codec, 1)));
        CHECK(record.GetRecordFrames() == 1);
        CHECK_FALSE(record.RecodeFrame(Packet(Annex({aud, sei}), codec, 2)));
        CHECK(record.GetRecordFrames() == 1);
        CHECK(record.GetLastIndex() == 2);
    }
    Reader reader(env.File("_video.mp4"));
    reader.CheckParameters(codec);
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 1);
    reader.Check(1, Expected({aud, first, second, sei}), false);
}

TEST_CASE("MP4 recording rejects the whole malformed packet and recovers", "[mp4-record]") {
    auto codec   = GENERATE(Codec::kH264, Codec::kH265);
    auto invalid = GENERATE(0, 1, 2, 3, 4);
    Environment env;
    auto first = Nalu(codec, true, 256);
    auto last  = Nalu(codec, false, 16, 0xcc);
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        Configure(record, codec);
        REQUIRE(record.RecodeFrame(Packet(Annex({first}), codec, 1)));
        auto bad = Annex({Nalu(codec, false)});
        if (invalid == 0)
            bad.insert(bad.end(), {0, 0, 1});
        if (invalid == 1)
            bad.insert(bad.begin(), 0xff);
        if (invalid == 2)
            bad = {0, 0, 1, 0, 0, 1, 0x65};
        if (invalid == 3)
            bad.clear();
        if (invalid == 4)
            bad = codec == Codec::kH265 ? Bytes{0, 0, 1, 0x02} : Bytes{0, 0, 1};
        REQUIRE_FALSE(record.RecodeFrame(Packet(bad, codec, 2)));
        REQUIRE(record.GetRecordFrames() == 1);
        REQUIRE_FALSE(record.RecodeFrame(nullptr));
        auto wrong        = Packet(Annex({last}), codec, 2);
        wrong->stream_idx = 5;
        REQUIRE_FALSE(record.RecodeFrame(wrong));
        REQUIRE(record.RecodeFrame(Packet(Annex({last}), codec, 3)));
        CHECK(record.GetRecordFrames() == 2);
    }
    Reader reader(env.File("_video.mp4"));
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 2);
    reader.Check(1, Expected({first}), true);
    reader.Check(2, Expected({last}), false);
}

TEST_CASE("MP4 recording accepts maximum input with length prefix expansion", "[mp4-record]") {
    auto codec = GENERATE(Codec::kH264, Codec::kH265);
    Environment env;
    constexpr size_t limit = 64 * 64 * 3 / 2;
    auto exact             = Nalu(codec, true, limit - 3);
    auto oversized         = Nalu(codec, true, limit - 2);
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 64, 64);
        Configure(record, codec);
        REQUIRE_FALSE(record.RecodeFrame(Packet(Annex({oversized}, true), codec, 1)));
        REQUIRE(record.RecodeFrame(Packet(Annex({exact}, true), codec, 2)));
        REQUIRE(record.GetRecordFrames() == 1);
    }
    Reader reader(env.File("_video.mp4"));
    reader.Check(1, Expected({exact}), true);
}

TEST_CASE("MP4 write failures stop the instance and retain its temporary recording", "[mp4-record]") {
    auto fault     = GENERATE(Fault::False, Fault::Exception, Fault::NoSample);
    auto preceding = GENERATE(0, 1);
    Environment env;
    auto codec = Codec::kH264;
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        Configure(record, codec);
        if (preceding)
            REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 1)));
        env.writer.SetFault(fault);
        REQUIRE_FALSE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 2)));
        auto calls = env.writer.Addresses().size();
        env.writer.SetFault(Fault::None);
        REQUIRE_FALSE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 3)));
        CHECK(env.writer.Addresses().size() == calls);
        CHECK(record.GetRecordFrames() == preceding);
    }
    CHECK(fs::exists(env.File("_video.tmp")));
    CHECK_FALSE(fs::exists(env.File("_video.mp4")));
    CHECK_FALSE(fs::exists(env.File("_overview.json")));
}

TEST_CASE("MP4 empty recording removes its temporary file", "[mp4-record]") {
    Environment env;
    {
        cosmo::AlgMp4Record record(Codec::kH264, env.Param(), 25, 640, 480);
        Configure(record, Codec::kH264);
    }
    CHECK_FALSE(fs::exists(env.File("_video.tmp")));
    CHECK_FALSE(fs::exists(env.File("_video.mp4")));
}

TEST_CASE("MP4 write failure rejects an inline configured first sample", "[mp4-record]") {
    const auto codec = GENERATE(Codec::kH264, Codec::kH265);
    const auto fault = GENERATE(Fault::False, Fault::NoSample);
    Environment env;
    auto input = Parameters(codec);
    input.push_back(Nalu(codec, true));
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        env.writer.SetFault(fault);
        CHECK_FALSE(record.RecodeFrame(Packet(Annex(input), codec, 1)));
        CHECK(record.GetRecordFrames() == 0);
        const auto calls = env.writer.Addresses().size();
        env.writer.SetFault(Fault::None);
        CHECK_FALSE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 2)));
        CHECK(env.writer.Addresses().size() == calls);
    }
    CHECK(fs::exists(env.File("_video.tmp")));
    CHECK_FALSE(fs::exists(env.File("_video.mp4")));
}

TEST_CASE("MP4 records supplied real media in independent instances", "[.][mp4-record-media]") {
    const char* input  = std::getenv("COSMO_MP4_RECORD_INPUT");
    const char* output = std::getenv("COSMO_MP4_RECORD_OUTPUT");
    REQUIRE(input != nullptr);
    REQUIRE(output != nullptr);
    REQUIRE(fs::is_regular_file(input));
    REQUIRE(fs::is_directory(output));
    REQUIRE_FALSE(fs::exists(fs::path(output) / "a_video.mp4"));
    REQUIRE_FALSE(fs::exists(fs::path(output) / "b_video.mp4"));

    struct MediaInput {
        AVFormatContext* format{};
        AVBSFContext* filter{};
        AVPacket* packet   = av_packet_alloc();
        AVPacket* filtered = av_packet_alloc();
        ~MediaInput() {
            av_packet_free(&filtered);
            av_packet_free(&packet);
            av_bsf_free(&filter);
            avformat_close_input(&format);
        }
    } media;
    REQUIRE(media.packet != nullptr);
    REQUIRE(media.filtered != nullptr);
    REQUIRE(avformat_open_input(&media.format, input, nullptr, nullptr) == 0);
    REQUIRE(avformat_find_stream_info(media.format, nullptr) >= 0);
    const int stream_index = av_find_best_stream(media.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(stream_index >= 0);
    const auto* stream     = media.format->streams[stream_index];
    const auto* parameters = stream->codecpar;
    REQUIRE((parameters->codec_id == AV_CODEC_ID_H264 || parameters->codec_id == AV_CODEC_ID_HEVC));
    const bool hevc   = parameters->codec_id == AV_CODEC_ID_HEVC;
    const Codec codec = hevc ? Codec::kH265 : Codec::kH264;
    const double fps  = av_q2d(stream->avg_frame_rate);
    REQUIRE(fps >= 0.1);
    REQUIRE(fps <= 120);
    const auto* filter = av_bsf_get_by_name(hevc ? "hevc_mp4toannexb" : "h264_mp4toannexb");
    REQUIRE(filter != nullptr);
    REQUIRE(av_bsf_alloc(filter, &media.filter) == 0);
    REQUIRE(avcodec_parameters_copy(media.filter->par_in, parameters) == 0);
    media.filter->time_base_in = stream->time_base;
    REQUIRE(av_bsf_init(media.filter) == 0);
    Environment env;
    int64_t samples = 0;
    {
        cosmo::AlgMp4Record first(codec, env.Param("a"), static_cast<float>(fps), parameters->width,
                                  parameters->height);
        cosmo::AlgMp4Record second(codec, env.Param("b"), static_cast<float>(fps), parameters->width,
                                   parameters->height);
        const auto consume = [&] {
            int result;
            while ((result = av_bsf_receive_packet(media.filter, media.filtered)) >= 0) {
                REQUIRE(media.filtered->size > 0);
                auto packet = Packet(Bytes(media.filtered->data, media.filtered->data + media.filtered->size),
                                     codec, samples + 1);
                packet->timestamp = static_cast<int64_t>(1000.0 * static_cast<double>(samples) / fps);
                REQUIRE(first.RecodeFrame(packet));
                REQUIRE(second.RecodeFrame(packet));
                ++samples;
                av_packet_unref(media.filtered);
            }
            REQUIRE((result == AVERROR(EAGAIN) || result == AVERROR_EOF));
        };
        int result;
        while ((result = av_read_frame(media.format, media.packet)) >= 0) {
            if (media.packet->stream_index == stream_index) {
                REQUIRE(av_bsf_send_packet(media.filter, media.packet) == 0);
                consume();
            }
            av_packet_unref(media.packet);
        }
        REQUIRE(result == AVERROR_EOF);
        REQUIRE(av_bsf_send_packet(media.filter, nullptr) == 0);
        consume();
        REQUIRE(samples > 0);
        REQUIRE(first.GetRecordFrames() == samples);
        REQUIRE(second.GetRecordFrames() == samples);
    }
    for (const auto* id : {"a", "b"}) {
        const auto path = env.File("_video.mp4", id);
        Reader reader(path);
        REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == samples);
        REQUIRE(MP4GetTrackDuration(reader.handle, reader.track) ==
                static_cast<uint64_t>(samples) * static_cast<uint64_t>(90000.0 / fps));
        REQUIRE(fs::copy_file(path, fs::path(output) / (std::string(id) + "_video.mp4")));
    }
}

TEST_CASE("MP4 instances preserve independent samples and buffer lifetimes", "[mp4-record]") {
    Environment env;
    auto codec = Codec::kH264;
    auto a     = Nalu(codec, true, 256, 0xaa);
    auto b     = Nalu(codec, true, 256, 0xbb);
    {
        auto first = std::make_unique<cosmo::AlgMp4Record>(codec, env.Param("a"), 25, 640, 480);
        cosmo::AlgMp4Record second(codec, env.Param("b"), 25, 640, 480);
        Configure(*first, codec);
        Configure(second, codec);
        REQUIRE(first->RecodeFrame(Packet(Annex({a}), codec, 1)));
        REQUIRE(second.RecodeFrame(Packet(Annex({b}), codec, 1)));
        auto addresses = env.writer.Addresses();
        REQUIRE(addresses.size() == 2);
        CHECK(addresses[0] != addresses[1]);
        first.reset();
        REQUIRE(second.RecodeFrame(Packet(Annex({b}), codec, 2)));
        CHECK(second.GetRecordFrames() == 2);
    }
    Reader first(env.File("_video.mp4", "a"));
    first.Check(1, Expected({a}), true);
    Reader second(env.File("_video.mp4", "b"));
    REQUIRE(MP4GetTrackNumberOfSamples(second.handle, second.track) == 2);
    second.Check(1, Expected({b}), true);
    second.Check(2, Expected({b}), true);
}

TEST_CASE("MP4 rename failure retains the temporary recording without overview publication", "[mp4-record]") {
    Environment env;
    auto param = env.Param();
    fs::create_directory(env.File("_video.mp4"));
    {
        cosmo::AlgMp4Record record(Codec::kH264, param, 25, 640, 480);
        Configure(record, Codec::kH264);
        REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(Codec::kH264, true)}), Codec::kH264, 1)));
    }
    CHECK(fs::exists(env.File("_video.tmp")));
    CHECK(fs::is_directory(env.File("_video.mp4")));
    CHECK_FALSE(fs::exists(env.File("_overview.json")));
}

TEST_CASE("MP4 channel continues after a parameter-only packet", "[mp4-record][concurrency]") {
    Environment env;
    cosmo::CfgAlarmParamVideoRecordInfo duration{};
    duration.preDuration   = 5;
    duration.aftreDuration = 5;
    auto expectation       = NAMED_ALLOW_CALL(env.config, GetAlarmVideoDuration()).RETURN(duration);
    auto param             = env.Param();
    auto codec             = Codec::kH264;
    auto initial           = Parameters(codec);
    auto idr               = Nalu(codec, true);
    initial.push_back(idr);
    auto p = Nalu(codec, false, 20, 0xbb);
    {
        cosmo::AlgChannelMp4 channel("channel");
        channel.SetSize(640, 480);
        channel.SetFps(25);
        REQUIRE(channel.RecordMp4(param));
        auto first        = Packet(Annex(initial), codec, 1);
        first->is_i_frame = true;
        REQUIRE(channel.TaskFrame(first));
        REQUIRE(env.writer.WaitForCalls(1));
        REQUIRE(channel.TaskFrame(Packet(Annex(Parameters(codec)), codec, 2)));
        REQUIRE(channel.TaskFrame(Packet(Annex({p}), codec, 3)));
        // The callback must reach the second real write. Queue shutdown then
        // joins it before the recording is read; no timing sleep is needed.
        REQUIRE(env.writer.WaitForCalls(2));
    }
    Reader reader(env.File("_video.mp4"));
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 2);
    reader.Check(1, Expected({idr}), true);
    reader.Check(2, Expected({p}), false);
}

TEST_CASE("MP4 complete preflight does not submit parameters from a malformed packet", "[mp4-record]") {
    auto codec = GENERATE(Codec::kH264, Codec::kH265);
    Environment env;
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        auto bad = Annex(Parameters(codec));
        bad.insert(bad.end(), {0, 0, 1});
        REQUIRE_FALSE(record.RecodeFrame(Packet(bad, codec, 0)));
        REQUIRE_FALSE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 1)));
        REQUIRE(record.GetRecordFrames() == 0);
        Configure(record, codec);
        REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 2)));
    }
    Reader reader(env.File("_video.mp4"));
    reader.CheckParameters(codec);
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 1);
}

TEST_CASE("MP4 preserves emulation prevention and accepts legal Annex B padding", "[mp4-record]") {
    auto codec = GENERATE(Codec::kH264, Codec::kH265);
    Environment env;
    auto nalu = Nalu(codec, true);
    nalu.insert(nalu.end(), {0, 0, 3, 1, 0x80});
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        Configure(record, codec);
        auto padded = Annex({nalu});
        padded.insert(padded.begin(), 3, 0);
        padded.insert(padded.end(), 4, 0);
        REQUIRE(record.RecodeFrame(Packet(padded, codec, 1)));
    }
    Reader reader(env.File("_video.mp4"));
    reader.Check(1, Expected({nalu}), true);
}

TEST_CASE("MP4 HEVC IRAP range and mixed slices determine synchronization", "[mp4-record]") {
    auto type  = GENERATE(16, 17, 18, 19, 20, 21, 22, 23);
    auto mixed = GENERATE(false, true);
    Environment env;
    auto codec = Codec::kH265;
    auto first = Nalu(codec, true);
    first[0]   = static_cast<uint8_t>(type << 1);
    std::vector<Bytes> nalus{first};
    if (mixed)
        nalus.push_back(Nalu(codec, false));
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        Configure(record, codec);
        REQUIRE(record.RecodeFrame(Packet(Annex(nalus), codec, 1)));
    }
    Reader reader(env.File("_video.mp4"));
    reader.Check(1, Expected(nalus), type <= 21 && !mixed);
}

TEST_CASE("MP4 validates parameter length before narrowing and accepts later configuration", "[mp4-record]") {
    auto codec    = GENERATE(Codec::kH264, Codec::kH265);
    auto too_long = GENERATE(false, true);
    Environment env;
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        auto params = Parameters(codec);
        // The first parameter seeds the track; neither a truncated structure nor
        // a length that cannot fit MP4v2's uint16_t is admissible.
        params[0].resize(too_long ? 65536 : 3, 0x80);
        REQUIRE_FALSE(record.RecodeFrame(Packet(Annex(params), codec, 0)));
        REQUIRE_FALSE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 1)));
        Configure(record, codec);
        REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 2)));
    }
    Reader reader(env.File("_video.mp4"));
    reader.CheckParameters(codec);
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 1);
}

TEST_CASE("MP4 writer failure leaves another live instance operational", "[mp4-record]") {
    Environment env;
    auto codec = Codec::kH264;
    {
        cosmo::AlgMp4Record failing(codec, env.Param("failed"), 25, 640, 480);
        cosmo::AlgMp4Record healthy(codec, env.Param("healthy"), 25, 640, 480);
        Configure(failing, codec);
        Configure(healthy, codec);
        env.writer.SetFault(Fault::False);
        REQUIRE_FALSE(failing.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 1)));
        env.writer.SetFault(Fault::None);
        REQUIRE(healthy.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 1)));
        REQUIRE_FALSE(failing.RecodeFrame(Packet(Annex({Nalu(codec, true)}), codec, 2)));
        REQUIRE(healthy.RecodeFrame(Packet(Annex({Nalu(codec, false)}), codec, 2)));
        CHECK(failing.GetRecordFrames() == 0);
        CHECK(healthy.GetRecordFrames() == 2);
    }
    CHECK(fs::exists(env.File("_video.tmp", "failed")));
    CHECK_FALSE(fs::exists(env.File("_video.mp4", "failed")));
    Reader reader(env.File("_video.mp4", "healthy"));
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 2);
    reader.Check(1, Expected({Nalu(codec, true)}), true);
    reader.Check(2, Expected({Nalu(codec, false)}), false);
}
