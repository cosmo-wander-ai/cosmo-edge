#include <SQLiteCpp/SQLiteCpp.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

extern "C" {
#include "libavcodec/bsf.h"
#include "libavformat/avformat.h"
}

#include "catch_amalgamated.hpp"
#include "flow/channel/AlgChannelMp4.h"
#include "flow/channel/AlgMp4Record.h"
#include "flow/stream/RtmpStreamPusher.h"
#include "media/VideoDemuxer.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockDbService.h"
#include "mock/MockTaskService.h"
#include "mp4v2/mp4v2.h"
#include "nlohmann/json.hpp"
#include "service/event/AlarmMediaActivity.h"
#include "service/event/impl/AlarmRecordServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "support/bframes_fixture.h"
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

enum class Fault { None, False, Exception, NoSample, Block };
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
        writer_state.changed.notify_all();
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
Bytes Configuration(Codec codec, const std::vector<Bytes>& parameters) {
    Bytes result;
    const auto append = [&](const Bytes& nalu) {
        REQUIRE(nalu.size() <= 65535);
        result.push_back(static_cast<uint8_t>(nalu.size() >> 8));
        result.push_back(static_cast<uint8_t>(nalu.size()));
        result.insert(result.end(), nalu.begin(), nalu.end());
    };
    if (codec == Codec::kH264) {
        result = {1, parameters[0][1], parameters[0][2], parameters[0][3], 0xff, 0xe1};
        append(parameters[0]);
        result.push_back(1);
        append(parameters[1]);
    } else {
        result.resize(23);
        result[0]  = 1;
        result[21] = 3;
        result[22] = 3;
        for (size_t index = 0; index < parameters.size(); ++index) {
            result.push_back(static_cast<uint8_t>(32 + index));
            result.insert(result.end(), {0, 1});
            append(parameters[index]);
        }
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
        std::unique_lock<std::mutex> lock(writer_state.mutex);
        writer_state.addresses.push_back(bytes);
        fault = writer_state.fault;
        writer_state.changed.notify_all();
        if (fault == Fault::Block) {
            writer_state.changed.wait(lock, [] { return writer_state.fault != Fault::Block; });
            fault = writer_state.fault;
        }
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

TEST_CASE("MP4 recording preserves B-frame composition offsets and decode intervals",
          "[mp4-record][packet-time]") {
    Environment env;
    const auto codec = Codec::kH264;
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        Configure(record, codec);
        const std::array<int64_t, 4> dts{0, 3600, 7200, 10800};
        const std::array<int64_t, 4> offsets{7200, 10800, 1, 3600};
        for (size_t i = 0; i < dts.size(); ++i) {
            auto packet           = Packet(Annex({Nalu(codec, i == 0)}), codec, i + 1);
            packet->pts           = dts[i] + offsets[i];
            packet->dts           = dts[i];
            packet->duration      = 3600;
            packet->time_base_num = 1;
            packet->time_base_den = 90000;
            const bool written    = record.RecodeFrame(packet);
            CHECK(written == (i != 0));
        }
        CHECK(record.GetRecordFrames() == 3);
    }
    Reader reader(env.File("_video.mp4"));
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 4);
    const std::array<uint64_t, 4> offsets{7200, 10800, 1, 3600};
    for (size_t i = 0; i < offsets.size(); ++i) {
        CHECK(MP4GetSampleRenderingOffset(reader.handle, reader.track, i + 1) == offsets[i]);
        CHECK(MP4GetSampleDuration(reader.handle, reader.track, i + 1) == 3600);
        CHECK(MP4GetSampleSize(reader.handle, reader.track, i + 1) == 20);
    }
}

TEST_CASE("MP4 source clock falls back only when the first frame lacks timestamps",
          "[mp4-record][packet-time]") {
    const bool missing_pts = GENERATE(false, true);
    Environment env;
    {
        cosmo::AlgMp4Record record(Codec::kH264, env.Param(), 25, 640, 480);
        Configure(record, Codec::kH264);
        auto first           = Packet(Annex({Nalu(Codec::kH264, true)}), Codec::kH264, 1);
        first->time_base_num = 1;
        first->time_base_den = 90000;
        first->dts           = missing_pts ? 0 : std::numeric_limits<int64_t>::min();
        first->pts           = missing_pts ? std::numeric_limits<int64_t>::min() : 0;
        REQUIRE(record.RecodeFrame(first));
        auto next           = Packet(Annex({Nalu(Codec::kH264, false)}), Codec::kH264, 2);
        next->time_base_num = 1;
        next->time_base_den = 90000;
        next->pts = next->dts = 3600;
        REQUIRE(record.RecodeFrame(next));
    }
    Reader reader(env.File("_video.mp4"));
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 2);
    CHECK(MP4GetSampleRenderingOffset(reader.handle, reader.track, 1) == 0);
    CHECK(MP4GetSampleDuration(reader.handle, reader.track, 1) == 3600);
}

TEST_CASE("MP4 rejects a missing timestamp after a valid source clock", "[mp4-record][packet-time]") {
    Environment env;
    {
        cosmo::AlgMp4Record record(Codec::kH264, env.Param(), 25, 640, 480);
        Configure(record, Codec::kH264);
        auto first           = Packet(Annex({Nalu(Codec::kH264, true)}), Codec::kH264, 1);
        first->time_base_num = 1;
        first->time_base_den = 90000;
        first->pts = first->dts = 0;
        first->duration         = 3600;
        CHECK_FALSE(record.RecodeFrame(first));
        auto next           = Packet(Annex({Nalu(Codec::kH264, false)}), Codec::kH264, 2);
        next->time_base_num = 1;
        next->time_base_den = 90000;
        next->pts           = 3600;
        next->dts           = std::numeric_limits<int64_t>::min();
        CHECK_FALSE(record.RecodeFrame(next));
    }
    CHECK(fs::exists(env.File("_video.tmp")));
    CHECK_FALSE(fs::exists(env.File("_video.mp4")));
}

TEST_CASE("MP4 recording seeds SDP-only AVC and HEVC parameters for every event",
          "[mp4-record][codec-headers]") {
    const auto codec       = GENERATE(Codec::kH264, Codec::kH265);
    const bool config_form = GENERATE(false, true);
    Environment env;
    const auto parameters = Parameters(codec);
    const auto headers =
        std::make_shared<const Bytes>(config_form ? Configuration(codec, parameters) : Annex(parameters));
    const auto idr = Annex({Nalu(codec, true)});
    const auto p   = Annex({Nalu(codec, false)});
    for (const auto* id : {"first", "later"}) {
        {
            cosmo::AlgMp4Record record(codec, env.Param(id), 25, 640, 480);
            auto before             = Packet(p, codec, 0);
            before->codec_extradata = headers;
            REQUIRE_FALSE(record.RecodeFrame(before));
            REQUIRE(record.GetRecordFrames() == 0);
            auto first             = Packet(idr, codec, 1);
            first->codec_extradata = headers;
            REQUIRE(record.RecodeFrame(first));
            CHECK(first->data == idr);  // Other packet consumers still see the original bytes.
            auto second             = Packet(p, codec, 2);
            second->codec_extradata = headers;
            REQUIRE(record.RecodeFrame(second));
            REQUIRE(record.GetRecordFrames() == 2);
        }
        Reader reader(env.File("_video.mp4", id));
        reader.CheckParameters(codec);
        REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 2);
        reader.Check(1, Expected({Nalu(codec, true)}), true);
        reader.Check(2, Expected({Nalu(codec, false)}), false);
    }
}

TEST_CASE("MP4 recording preserves complete in-band parameters with extradata attached",
          "[mp4-record][codec-headers]") {
    const auto codec = GENERATE(Codec::kH264, Codec::kH265);
    Environment env;
    auto input = Parameters(codec);
    input.push_back(Nalu(codec, true));
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        auto packet             = Packet(Annex(input), codec, 1);
        packet->codec_extradata = std::make_shared<const Bytes>(Configuration(codec, Parameters(codec)));
        REQUIRE(record.RecodeFrame(packet));
        CHECK(record.GetRecordFrames() == 1);
    }
    Reader reader(env.File("_video.mp4"));
    reader.CheckParameters(codec);
    reader.Check(1, Expected({Nalu(codec, true)}), true);
}

TEST_CASE("MP4 recording rejects invalid SDP and ignores another stream's configuration",
          "[mp4-record][codec-headers]") {
    const auto codec = GENERATE(Codec::kH264, Codec::kH265);
    Environment env;
    const auto bad_headers = std::make_shared<const Bytes>(Bytes{1, 0xff});
    SECTION("invalid configuration cannot publish an apparently successful video") {
        {
            cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
            auto packet             = Packet(Annex({Nalu(codec, true)}), codec, 1);
            packet->codec_extradata = bad_headers;
            CHECK_FALSE(record.RecodeFrame(packet));
            CHECK(record.GetRecordFrames() == 0);
            auto valid = Parameters(codec);
            valid.push_back(Nalu(codec, true));
            CHECK_FALSE(record.RecodeFrame(Packet(Annex(valid), codec, 2)));
        }
        CHECK(fs::exists(env.File("_video.tmp")));
        CHECK_FALSE(fs::exists(env.File("_video.mp4")));
    }
    SECTION("a queued packet from another generation cannot seed the recorder") {
        {
            cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
            auto wrong             = Packet(Annex({Nalu(codec, true)}), codec, 1);
            wrong->stream_idx      = 1;
            wrong->codec_extradata = bad_headers;
            CHECK_FALSE(record.RecodeFrame(wrong));
            auto own             = Packet(Annex({Nalu(codec, true)}), codec, 1);
            own->codec_extradata = std::make_shared<const Bytes>(Annex(Parameters(codec)));
            REQUIRE(record.RecodeFrame(own));
        }
        Reader reader(env.File("_video.mp4"));
        reader.CheckParameters(codec);
        reader.Check(1, Expected({Nalu(codec, true)}), true);
    }
}

TEST_CASE("MP4 recording does not publish a failed SDP-seeded first sample", "[mp4-record][codec-headers]") {
    const auto codec = GENERATE(Codec::kH264, Codec::kH265);
    const auto fault = GENERATE(Fault::False, Fault::NoSample);
    Environment env;
    {
        cosmo::AlgMp4Record record(codec, env.Param(), 25, 640, 480);
        auto packet             = Packet(Annex({Nalu(codec, true)}), codec, 1);
        packet->codec_extradata = std::make_shared<const Bytes>(Annex(Parameters(codec)));
        env.writer.SetFault(fault);
        REQUIRE_FALSE(record.RecodeFrame(packet));
        CHECK(record.GetRecordFrames() == 0);
        const auto calls = env.writer.Addresses().size();
        env.writer.SetFault(Fault::None);
        CHECK_FALSE(record.RecodeFrame(packet));
        CHECK(env.writer.Addresses().size() == calls);
    }
    CHECK(fs::exists(env.File("_video.tmp")));
    CHECK_FALSE(fs::exists(env.File("_video.mp4")));
}

TEST_CASE("Demuxed recording packets retain their codec configuration after reopen",
          "[mp4-record][codec-headers][demux][reopen]") {
    Environment env;
    {
        cosmo::AlgMp4Record record(Codec::kH264, env.Param(), 25, 640, 480);
        Configure(record, Codec::kH264);
        REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(Codec::kH264, true)}), Codec::kH264, 1)));
        REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(Codec::kH264, false)}), Codec::kH264, 2)));
    }
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(env.File("_video.mp4").string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    const auto headers = demux.GetCodecExtradata();
    REQUIRE_FALSE(headers.empty());
    auto old_packet = std::make_shared<cosmo::media::VideoPacket>();
    REQUIRE(demux.Demux(old_packet) == cosmo::media::ReadFrameStatus::Success);
    REQUIRE(old_packet->codec_extradata);
    CHECK(*old_packet->codec_extradata == headers);
    auto same_stream = std::make_shared<cosmo::media::VideoPacket>();
    REQUIRE(demux.Demux(same_stream) == cosmo::media::ReadFrameStatus::Success);
    CHECK(same_stream->codec_extradata == old_packet->codec_extradata);
    demux.CloseStream();
    CHECK(demux.GetCodecExtradata().empty());
    CHECK(*old_packet->codec_extradata == headers);
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    auto new_packet = std::make_shared<cosmo::media::VideoPacket>();
    REQUIRE(demux.Demux(new_packet) == cosmo::media::ReadFrameStatus::Success);
    REQUIRE(new_packet->codec_extradata);
    CHECK(new_packet->stream_idx != old_packet->stream_idx);
    CHECK(new_packet->codec_extradata != old_packet->codec_extradata);
    CHECK(*new_packet->codec_extradata == headers);
    CHECK(*old_packet->codec_extradata == headers);
}

TEST_CASE("Real B-frame file retains display order through recording and raw preview",
          "[mp4-record][rtmp][packet-time][demux]") {
    Environment env;
    const auto source = fs::path(env.root) / "bframes-source.mp4";
    fs::create_directories(source.parent_path());
    {
        std::ofstream output(source, std::ios::binary);
        output.write(reinterpret_cast<const char*>(kBFramesMp4), sizeof(kBFramesMp4));
        REQUIRE(output.good());
    }
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(source.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    auto packet = std::make_shared<cosmo::media::VideoPacket>();
    REQUIRE(demux.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
    REQUIRE(packet->time_base_num > 0);
    REQUIRE(packet->time_base_den > 0);
    auto param          = env.Param();
    param.streamIndex   = packet->stream_idx;
    const auto raw_path = fs::path(env.root) / "bframes-preview.flv";
    {
        cosmo::AlgMp4Record record(Codec::kH264, param, 25, 64, 64);
        cosmo::RtmpStreamPusher raw(Codec::kH264, raw_path.string(), 64, 64, 25);
        for (int i = 0; i < 24; ++i) {
            REQUIRE(packet->time_base_num > 0);
            REQUIRE(packet->time_base_den > 0);
            CHECK(packet->pts != std::numeric_limits<int64_t>::min());
            CHECK(packet->dts != std::numeric_limits<int64_t>::min());
            raw.PushPacket(*packet);
            record.RecodeFrame(packet);
            if (i != 23) {
                packet = std::make_shared<cosmo::media::VideoPacket>();
                REQUIRE(demux.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
            }
        }
        CHECK(raw.GetProcInfo().sendFrames == 24);
        raw.Stop();
    }
    const auto verify_decode = [](const fs::path& path) {
        AVFormatContext* input = nullptr;
        REQUIRE(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
        REQUIRE(avformat_find_stream_info(input, nullptr) >= 0);
        const int index = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        REQUIRE(index >= 0);
        auto* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        REQUIRE(codec != nullptr);
        auto* decoder = avcodec_alloc_context3(codec);
        REQUIRE(decoder != nullptr);
        REQUIRE(avcodec_parameters_to_context(decoder, input->streams[index]->codecpar) == 0);
        decoder->thread_count = 1;
        REQUIRE(avcodec_open2(decoder, codec, nullptr) == 0);
        auto* frame = av_frame_alloc();
        auto* pkt   = av_packet_alloc();
        REQUIRE(frame != nullptr);
        REQUIRE(pkt != nullptr);
        int frames       = 0;
        int64_t last     = std::numeric_limits<int64_t>::min();
        const auto drain = [&] {
            int ret;
            while ((ret = avcodec_receive_frame(decoder, frame)) == 0) {
                REQUIRE(frame->best_effort_timestamp != AV_NOPTS_VALUE);
                CHECK(frame->best_effort_timestamp > last);
                last = frame->best_effort_timestamp;
                ++frames;
                av_frame_unref(frame);
            }
            REQUIRE((ret == AVERROR(EAGAIN) || ret == AVERROR_EOF));
        };
        while (av_read_frame(input, pkt) == 0) {
            if (pkt->stream_index == index) {
                REQUIRE(avcodec_send_packet(decoder, pkt) == 0);
                drain();
            }
            av_packet_unref(pkt);
        }
        REQUIRE(avcodec_send_packet(decoder, nullptr) == 0);
        drain();
        CHECK(frames == 24);
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&decoder);
        avformat_close_input(&input);
    };
    verify_decode(env.File("_video.mp4"));
    verify_decode(raw_path);
    demux.CloseStream();
    demux.SetForceFps(5);
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    CHECK(demux.GetFPS() == 5);
    auto forced_packet = std::make_shared<cosmo::media::VideoPacket>();
    REQUIRE(demux.Demux(forced_packet) == cosmo::media::ReadFrameStatus::Success);
    CHECK(forced_packet->time_base_num == 0);
    CHECK(forced_packet->time_base_den == 0);
}

TEST_CASE("MP4 recording keeps its event busy until finalization", "[mp4-record][alarm-delete]") {
    Environment env;
    const auto param = env.Param("busy-event");
    {
        cosmo::AlgMp4Record recording(Codec::kH264, param, 25, 640, 480);
        REQUIRE(recording.IsReady());
        CHECK_FALSE(cosmo::service::AlarmMediaLease::TryDelete(param.recordId));
    }
    auto deletion = cosmo::service::AlarmMediaLease::TryDelete(param.recordId);
    REQUIRE(deletion);
    {
        cosmo::AlgMp4Record recording(Codec::kH264, param, 25, 640, 480);
        CHECK_FALSE(recording.IsReady());
        CHECK_FALSE(fs::exists(env.File("_video.tmp", param.recordId)));
    }
    deletion.reset();
    CHECK(cosmo::service::AlarmMediaLease::StartRecording(param.recordId));
}

TEST_CASE("Alarm deletion waits for the real MP4 recorder to finalize", "[mp4-record][alarm-delete]") {
    Environment env;
    auto database = std::make_shared<SQLite::Database>(
        ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
    cosmo::test::MockDbService dbService;
    cosmo::test::ScopedServiceOverride<cosmo::service::IDbService> registration{dbService};
    ALLOW_CALL(dbService, GetDb()).RETURN(database);
    cosmo::service::AlarmRecordServiceImpl alarms;
    const auto param   = env.Param("live-event");
    const auto feature = fs::path(cosmo::path::GetRecordRootPath(false)) / "2001/01/02/live-event.feature";
    fs::create_directories(feature.parent_path());
    std::ofstream(feature) << "synthetic feature";
    {
        cosmo::AlgMp4Record recording(Codec::kH264, param, 25, 640, 480);
        REQUIRE(recording.IsReady());
        Configure(recording, Codec::kH264);
        REQUIRE(recording.RecodeFrame(Packet(Annex({Nalu(Codec::kH264, true)}), Codec::kH264, 1)));
        cosmo::AlarmRecordUnit event;
        event.id         = param.recordId;
        event.category   = "2";
        event.timestamp  = param.frameTimestamp;
        event.extraFiles = "[]";
        event.property   = "{}";
        REQUIRE(alarms.Insert(event));
        const auto busy = alarms.DeleteEvents({param.recordId});
        REQUIRE(busy.failedItems.size() == 1);
        CHECK(busy.failedItems[0].reason == "RECORDING");
        CHECK(fs::exists(env.File("_video.tmp", param.recordId)));
        CHECK(fs::exists(feature));
    }
    REQUIRE(fs::exists(env.File("_video.mp4", param.recordId)));
    REQUIRE(fs::exists(env.File("_overview.json", param.recordId)));
    const auto removed = alarms.DeleteEvents({param.recordId});
    CHECK(removed.deletedIds == std::vector<std::string>{param.recordId});
    CHECK_FALSE(fs::exists(env.File("_video.mp4", param.recordId)));
    CHECK_FALSE(fs::exists(env.File(".json", param.recordId)));
    CHECK_FALSE(fs::exists(env.File("_overview.json", param.recordId)));
    CHECK_FALSE(fs::exists(feature));
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

TEST_CASE("MP4 metadata saves measured corners with source dimensions and legacy rectangles",
          "[mp4-record][geometry]") {
    const bool tracked = GENERATE(false, true);
    Environment env;
    auto param     = env.Param();
    param.targetId = tracked ? 7 : -1;
    cosmo::AiDetectRstEl measured;
    measured.trackId = 7;
    measured.box     = {-20, 100, 400, 200};
    measured.oriented_corners =
        cosmo::util::Quad{{{-20.5f, 100.25f}, {379.5f, 110.5f}, {359.25f, 300.75f}, {-40.75f, 290.5f}}};
    cosmo::AiDetectRstEl ordinary;
    ordinary.trackId = 9;
    ordinary.box     = {800, 400, 200, 100};
    cosmo::DataDetTrackClassify initial;
    initial.dataType   = tracked ? cosmo::AlgDataType::TaskDataTrack : cosmo::AlgDataType::ChannelDataDetect;
    initial.frameIndex = 0;
    initial.picWidth   = 1920;
    initial.picHeight  = 1080;
    initial.targets    = {measured, ordinary};
    auto later         = initial;
    later.frameIndex   = 2;
    later.picWidth     = 1280;
    later.picHeight    = 720;
    later.targets[0].oriented_corners.reset();  // Prediction-only target has no measured geometry.
    std::vector<cosmo::DataDetTrackClassify> history{initial, later};
    REQUIRE_CALL(env.tasks, GetTaskDetHistory("channel", "task", 0, param.startframeTimestamp, 2))
        .RETURN(history);
    {
        cosmo::AlgMp4Record record(Codec::kH264, param, 25, 640, 480);
        Configure(record, Codec::kH264);
        REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(Codec::kH264, true)}), Codec::kH264, 1)));
        REQUIRE(record.RecodeFrame(Packet(Annex({Nalu(Codec::kH264, false)}), Codec::kH264, 2)));
    }  // Actual recorder finalization writes the metadata file.
    const auto saved = nlohmann::json::parse(cosmo::util::ReadFile(param.jsonPath));
    REQUIRE(saved["targets"].size() == 3);
    const auto& first = saved["targets"][0];
    CHECK(first["sourceWidth"] == 1920);
    CHECK(first["sourceHeight"] == 1080);
    REQUIRE(first["rects"].size() == (tracked ? 1 : 2));
    const auto& rect = first["rects"][0];
    CHECK(rect["xRatio"] == 0.0);  // Preserve existing normalized/clamped AABB behavior.
    CHECK(rect["yRatio"].get<double>() == Catch::Approx(100.0 / 1080));
    CHECK(rect["wRatio"].get<double>() == Catch::Approx(400.0 / 1920));
    CHECK(rect["hRatio"].get<double>() == Catch::Approx(200.0 / 1080));
    REQUIRE(rect["orientedCorners"].size() == 4);
    CHECK(rect["orientedCorners"][0]["x"] == -20.5f);
    CHECK(rect["orientedCorners"][0]["y"] == 100.25f);
    if (!tracked)
        CHECK_FALSE(first["rects"][1].contains("orientedCorners"));
    // Existing gap filling repeats the complete preceding frame, including its
    // coordinate space; a subsequent AABB-only result cannot inherit corners.
    CHECK(saved["targets"][1]["sourceWidth"] == 1920);
    CHECK(saved["targets"][1]["rects"][0]["orientedCorners"] == rect["orientedCorners"]);
    CHECK(saved["targets"][2]["sourceWidth"] == 1280);
    CHECK(saved["targets"][2]["sourceHeight"] == 720);
    CHECK_FALSE(saved["targets"][2]["rects"][0].contains("orientedCorners"));
    const auto restored = saved.get<cosmo::MsgAlarmVideoOverviewInfo>();
    REQUIRE(restored.targets[0].rects[0].oriented_corners);
    CHECK(restored.targets[0].rects[0].oriented_corners->at(0).x == -20.5f);
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

TEST_CASE("MP4 channel does not replay a buffered first timed IDR",
          "[mp4-record][packet-time][concurrency]") {
    Environment env;
    cosmo::CfgAlarmParamVideoRecordInfo duration{};
    duration.preDuration   = 5;
    duration.aftreDuration = 5;
    auto expectation       = NAMED_ALLOW_CALL(env.config, GetAlarmVideoDuration()).RETURN(duration);
    auto param             = env.Param();
    auto first_nalus       = Parameters(Codec::kH264);
    first_nalus.push_back(Nalu(Codec::kH264, true));
    {
        cosmo::AlgChannelMp4 channel("channel");
        channel.SetSize(640, 480);
        channel.SetFps(25);
        REQUIRE(channel.RecordMp4(param));
        for (int i = 0; i < 3; ++i) {
            auto packet =
                Packet(i == 0 ? Annex(first_nalus) : Annex({Nalu(Codec::kH264, false)}), Codec::kH264, i + 1);
            packet->is_i_frame    = i == 0;
            packet->dts           = -7200 + i * 3600;
            packet->pts           = packet->dts + (i == 1 ? 10800 : 7200);
            packet->duration      = 3600;
            packet->time_base_num = 1;
            packet->time_base_den = 90000;
            REQUIRE(channel.TaskFrame(packet));
        }
        REQUIRE(env.writer.WaitForCalls(2));
    }
    Reader reader(env.File("_video.mp4"));
    REQUIRE(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 3);
    CHECK(MP4GetSampleRenderingOffset(reader.handle, reader.track, 1) == 7200);
    CHECK(MP4GetSampleRenderingOffset(reader.handle, reader.track, 2) == 10800);
    CHECK(MP4GetSampleRenderingOffset(reader.handle, reader.track, 3) == 7200);
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

TEST_CASE("Local B-frame repeat keeps one raw publisher and MP4 timeline", "[mp4-record][rtmp][local-loop]") {
    Environment env;
    const auto source = fs::path(env.root) / "loop-source.mp4";
    fs::create_directories(source.parent_path());
    {
        std::ofstream output(source, std::ios::binary);
        output.write(reinterpret_cast<const char*>(kBFramesMp4), sizeof(kBFramesMp4));
        REQUIRE(output.good());
    }
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(source.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    auto packet = std::make_shared<cosmo::media::VideoPacket>();
    REQUIRE(demux.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
    auto param          = env.Param();
    param.streamIndex   = packet->stream_idx;
    const auto stream   = packet->stream_idx;
    const auto raw_path = fs::path(env.root) / "loop-preview.flv";
    int64_t last_dts    = std::numeric_limits<int64_t>::min();
    std::vector<int64_t> offsets;
    std::vector<Bytes> source_packets;
    {
        cosmo::AlgMp4Record record(Codec::kH264, param, 25, 64, 64);
        cosmo::RtmpStreamPusher raw(Codec::kH264, raw_path.string(), 64, 64, 25);
        for (int loop = 0; loop < 3; ++loop) {
            for (int i = 0; i < 24; ++i) {
                if (loop != 0 || i != 0) {
                    packet = std::make_shared<cosmo::media::VideoPacket>();
                    REQUIRE(demux.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
                }
                CHECK(packet->stream_idx == stream);
                CHECK(packet->index == loop * 24 + i + 1);
                REQUIRE(packet->dts > last_dts);
                last_dts = packet->dts;
                if (loop == 0) {
                    offsets.push_back(packet->pts - packet->dts);
                    source_packets.push_back(packet->data);
                } else {
                    CHECK(packet->pts - packet->dts == offsets[static_cast<size_t>(i)]);
                    CHECK(packet->data == source_packets[static_cast<size_t>(i)]);
                }
                raw.PushPacket(*packet);
                REQUIRE(raw.IsReady());
                record.RecodeFrame(packet);
            }
            if (loop != 2) {
                REQUIRE(demux.Demux(std::make_shared<cosmo::media::VideoPacket>()) ==
                        cosmo::media::ReadFrameStatus::StreamEnd);
                REQUIRE(demux.CanContinueLocalLoop());
                demux.CloseStream(true);
                REQUIRE(demux.OpenStream(true) == cosmo::util::ErrorEnum::Success);
                REQUIRE(demux.FindStream(true) == cosmo::util::ErrorEnum::Success);
            }
        }
        CHECK(raw.GetProcInfo().sendFrames == 72);
        CHECK(record.GetRecordFrames() == 71);  // last sample is pending until close
        raw.Stop();
    }
    for (const auto& path : {raw_path, env.File("_video.mp4")}) {
        AVFormatContext* input = nullptr;
        REQUIRE(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
        REQUIRE(avformat_find_stream_info(input, nullptr) >= 0);
        const int index = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        REQUIRE(index >= 0);
        auto* decoder = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
        REQUIRE(decoder != nullptr);
        REQUIRE(avcodec_parameters_to_context(decoder, input->streams[index]->codecpar) == 0);
        decoder->thread_count = 1;
        REQUIRE(avcodec_open2(decoder, decoder->codec, nullptr) == 0);
        auto* frame   = av_frame_alloc();
        auto* encoded = av_packet_alloc();
        REQUIRE(frame != nullptr);
        REQUIRE(encoded != nullptr);
        int frames = 0, packets = 0;
        int64_t prior_dts = std::numeric_limits<int64_t>::min();
        int64_t prior_pts = std::numeric_limits<int64_t>::min();
        const auto drain  = [&] {
            int ret;
            while ((ret = avcodec_receive_frame(decoder, frame)) == 0) {
                REQUIRE(frame->best_effort_timestamp != AV_NOPTS_VALUE);
                CHECK(frame->best_effort_timestamp > prior_pts);
                prior_pts = frame->best_effort_timestamp;
                ++frames;
                av_frame_unref(frame);
            }
            REQUIRE((ret == AVERROR(EAGAIN) || ret == AVERROR_EOF));
        };
        while (av_read_frame(input, encoded) == 0) {
            if (encoded->stream_index == index) {
                REQUIRE(encoded->dts != AV_NOPTS_VALUE);
                CHECK(encoded->dts > prior_dts);
                prior_dts = encoded->dts;
                ++packets;
                REQUIRE(avcodec_send_packet(decoder, encoded) == 0);
                drain();
            }
            av_packet_unref(encoded);
        }
        REQUIRE(avcodec_send_packet(decoder, nullptr) == 0);
        drain();
        CHECK(packets == 72);
        CHECK(frames == 72);
        av_packet_free(&encoded);
        av_frame_free(&frame);
        avcodec_free_context(&decoder);
        avformat_close_input(&input);
    }
}

TEST_CASE("MP4 channel preserves a recording across local B-frame replay", "[mp4-record][local-loop]") {
    Environment env;
    const auto source = fs::path(env.root) / "loop-channel-source.mp4";
    fs::create_directories(source.parent_path());
    {
        std::ofstream output(source, std::ios::binary);
        output.write(reinterpret_cast<const char*>(kBFramesMp4), sizeof(kBFramesMp4));
        REQUIRE(output.good());
    }
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(source.string());
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    std::vector<VideoPacketPtr> packets;
    constexpr int64_t base = 1700000000000;
    for (int loop = 0; loop < 3; ++loop) {
        for (int i = 0; i < 24; ++i) {
            auto packet = std::make_shared<cosmo::media::VideoPacket>();
            REQUIRE(demux.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
            packet->timestamp = base + packet->index * 40;
            packets.push_back(packet);
        }
        if (loop != 2) {
            REQUIRE(demux.Demux(std::make_shared<cosmo::media::VideoPacket>()) ==
                    cosmo::media::ReadFrameStatus::StreamEnd);
            REQUIRE(demux.CanContinueLocalLoop());
            demux.CloseStream(true);
            REQUIRE(demux.OpenStream(true) == cosmo::util::ErrorEnum::Success);
            REQUIRE(demux.FindStream(true) == cosmo::util::ErrorEnum::Success);
        }
    }
    cosmo::CfgAlarmParamVideoRecordInfo duration{};
    duration.preDuration   = 5;
    duration.aftreDuration = 10;
    std::mutex mutex;
    std::condition_variable changed;
    bool first_processed = false;
    auto expectation     = NAMED_ALLOW_CALL(env.config, GetAlarmVideoDuration())
                           .LR_SIDE_EFFECT({
                               std::lock_guard<std::mutex> lock(mutex);
                               first_processed = true;
                               changed.notify_all();
                           })
                           .RETURN(duration);
    {
        cosmo::AlgChannelMp4 channel("channel");
        channel.SetSize(64, 64);
        channel.SetFps(25);
        REQUIRE(channel.TaskFrame(packets[0]));
        REQUIRE(channel.TaskFrame(packets[1]));
        {
            std::unique_lock<std::mutex> lock(mutex);
            REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] { return first_processed; }));
        }
        auto param           = env.Param();
        param.streamIndex    = packets[1]->stream_idx;
        param.frameTimestamp = packets[1]->timestamp;
        param.frameSeq       = packets[1]->index;
        REQUIRE(channel.RecordMp4(param));
        for (size_t i = 2; i < packets.size(); ++i)
            REQUIRE(channel.TaskFrame(packets[i]));
        REQUIRE(env.writer.WaitForCalls(70));
    }
    Reader reader(env.File("_video.mp4"));
    CHECK(MP4GetTrackNumberOfSamples(reader.handle, reader.track) == 72);
}

TEST_CASE("MP4 terminal marker survives a saturated recording queue", "[mp4-record][local-loop]") {
    Environment env;
    cosmo::CfgAlarmParamVideoRecordInfo duration{};
    duration.preDuration   = 5;
    duration.aftreDuration = 5;
    auto expectation       = NAMED_ALLOW_CALL(env.config, GetAlarmVideoDuration()).RETURN(duration);
    {
        cosmo::AlgChannelMp4 channel("channel");
        struct ReleaseBlock {
            ScopedWriter& writer;
            ~ReleaseBlock() {
                writer.SetFault(Fault::None);
            }
        } release{env.writer};
        channel.SetSize(640, 480);
        channel.SetFps(25);
        auto param = env.Param();
        REQUIRE(channel.RecordMp4(param));
        auto parameters = Parameters(Codec::kH264);
        parameters.push_back(Nalu(Codec::kH264, true));
        env.writer.SetFault(Fault::Block);
        REQUIRE(channel.TaskFrame(Packet(Annex(parameters), Codec::kH264, 1)));
        REQUIRE(env.writer.WaitForCalls(1));
        for (int64_t i = 2; i <= 301; ++i)
            REQUIRE(channel.TaskFrame(Packet(Annex({Nalu(Codec::kH264, false)}), Codec::kH264, i)));
        REQUIRE_FALSE(channel.TaskFrame(Packet(Annex({Nalu(Codec::kH264, false)}), Codec::kH264, 302)));
        auto end   = std::make_shared<cosmo::media::VideoPacket>();
        end->index = -1;
        REQUIRE(channel.TaskFrame(end));
        env.writer.SetFault(Fault::None);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!fs::exists(env.File("_video.mp4")) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        REQUIRE(fs::exists(env.File("_video.mp4")));
    }
}

TEST_CASE("Private local source repeats through raw and MP4 without presentation overlap",
          "[mp4-record][rtmp][local-loop][.private-local-source]") {
    const char* source = std::getenv("COSMO_LOCAL_LOOP_TEST_SOURCE");
    if (!source || !*source)
        SKIP("Set COSMO_LOCAL_LOOP_TEST_SOURCE to an absolute, private H264 source");
    REQUIRE(fs::path(source).is_absolute());
    Environment env;
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(source);
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    auto packet = std::make_shared<cosmo::media::VideoPacket>();
    REQUIRE(demux.Demux(packet) == cosmo::media::ReadFrameStatus::Success);
    auto param          = env.Param();
    param.streamIndex   = packet->stream_idx;
    const auto stream   = packet->stream_idx;
    const auto raw_path = fs::path(env.root) / "private-loop.flv";
    std::vector<Bytes> source_packets;
    std::vector<int64_t> source_cts;
    std::vector<int64_t> adjusted_cts;
    int64_t last_dts               = std::numeric_limits<int64_t>::min();
    int64_t prior_presentation_end = std::numeric_limits<int64_t>::min();
    int64_t source_tail_duration   = 0;
    const AVRational source_time_base{packet->time_base_num, packet->time_base_den};
    size_t packets_per_loop = 0;
    {
        cosmo::AlgMp4Record record(Codec::kH264, param, demux.GetFPS(), demux.GetWidth(), demux.GetHeight());
        cosmo::RtmpStreamPusher raw(Codec::kH264, raw_path.string(), demux.GetWidth(), demux.GetHeight(),
                                    demux.GetFPS());
        for (int loop = 0; loop < 3; ++loop) {
            size_t index           = 0;
            int64_t loop_cts_delta = 0;
            int64_t loop_min_pts   = std::numeric_limits<int64_t>::max();
            int64_t loop_max_end   = std::numeric_limits<int64_t>::min();
            for (;;) {
                if (loop != 0 || index != 0) {
                    packet            = std::make_shared<cosmo::media::VideoPacket>();
                    const auto result = demux.Demux(packet);
                    if (result == cosmo::media::ReadFrameStatus::StreamEnd)
                        break;
                    REQUIRE(result == cosmo::media::ReadFrameStatus::Success);
                }
                REQUIRE(packet->stream_idx == stream);
                REQUIRE(packet->dts > last_dts);
                REQUIRE(packet->duration > 0);
                REQUIRE(packet->pts >= prior_presentation_end);
                last_dts = packet->dts;
                if (loop == 0)
                    source_tail_duration = packet->duration;
                loop_min_pts = std::min(loop_min_pts, packet->pts);
                loop_max_end = std::max(loop_max_end, packet->pts + packet->duration);
                if (loop == 0) {
                    source_packets.push_back(packet->data);
                    source_cts.push_back(packet->pts - packet->dts);
                } else {
                    REQUIRE(index < source_packets.size());
                    CHECK(packet->data == source_packets[index]);
                    const auto delta = packet->pts - packet->dts - source_cts[index];
                    if (index == 0)
                        loop_cts_delta = delta;
                    CHECK(loop_cts_delta == 0);
                    CHECK(delta == loop_cts_delta);
                }
                adjusted_cts.push_back(packet->pts - packet->dts);
                raw.PushPacket(*packet);
                REQUIRE(raw.IsReady());
                const bool written = record.RecodeFrame(packet);
                if (loop == 0 && index == 0)
                    REQUIRE(record.HasStarted());  // First timed sample is pending until the next packet.
                else
                    REQUIRE(written);
                ++index;
            }
            if (loop == 0) {
                packets_per_loop = index;
                REQUIRE(packets_per_loop > 1);
            } else {
                CHECK(index == packets_per_loop);
                CHECK(loop_min_pts >= prior_presentation_end);
            }
            prior_presentation_end = loop_max_end;
            if (loop != 2) {
                REQUIRE(demux.CanContinueLocalLoop());
                demux.CloseStream(true);
                REQUIRE(demux.OpenStream(true) == cosmo::util::ErrorEnum::Success);
                REQUIRE(demux.FindStream(true) == cosmo::util::ErrorEnum::Success);
            }
        }
        CHECK(raw.GetProcInfo().sendFrames == packets_per_loop * 3);
        raw.Stop();
    }
    for (const auto& path : {raw_path, env.File("_video.mp4")}) {
        AVFormatContext* input = nullptr;
        REQUIRE(avformat_open_input(&input, path.c_str(), nullptr, nullptr) == 0);
        REQUIRE(avformat_find_stream_info(input, nullptr) >= 0);
        const int video = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        REQUIRE(video >= 0);
        auto* decoder = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
        REQUIRE(decoder != nullptr);
        REQUIRE(avcodec_parameters_to_context(decoder, input->streams[video]->codecpar) == 0);
        decoder->thread_count = 1;
        REQUIRE(avcodec_open2(decoder, decoder->codec, nullptr) == 0);
        auto* frame   = av_frame_alloc();
        auto* encoded = av_packet_alloc();
        REQUIRE(frame != nullptr);
        REQUIRE(encoded != nullptr);
        size_t frames = 0, packets = 0;
        int64_t previous_dts = std::numeric_limits<int64_t>::min();
        int64_t previous_pts = std::numeric_limits<int64_t>::min();
        const auto drain     = [&] {
            int ret;
            while ((ret = avcodec_receive_frame(decoder, frame)) == 0) {
                REQUIRE(frame->best_effort_timestamp != AV_NOPTS_VALUE);
                CHECK(frame->best_effort_timestamp > previous_pts);
                previous_pts = frame->best_effort_timestamp;
                ++frames;
                av_frame_unref(frame);
            }
            REQUIRE((ret == AVERROR(EAGAIN) || ret == AVERROR_EOF));
        };
        while (av_read_frame(input, encoded) == 0) {
            if (encoded->stream_index == video) {
                REQUIRE(encoded->dts != AV_NOPTS_VALUE);
                CHECK(encoded->dts > previous_dts);
                previous_dts = encoded->dts;
                REQUIRE(packets < adjusted_cts.size());
                REQUIRE(encoded->pts != AV_NOPTS_VALUE);
                const int64_t expected_cts =
                    av_rescale_q(adjusted_cts[packets], source_time_base, input->streams[video]->time_base);
                const int64_t output_cts = encoded->pts - encoded->dts;
                INFO("packet=" << packets << " output CTS=" << output_cts << " expected=" << expected_cts);
                CHECK(output_cts >= expected_cts - 1);
                CHECK(output_cts <= expected_cts + 1);
                if (path == env.File("_video.mp4") && (packets + 1) % packets_per_loop == 0) {
                    const int64_t expected_duration = av_rescale_q(source_tail_duration, source_time_base,
                                                                   input->streams[video]->time_base);
                    INFO("MP4 tail sample=" << packets << " actual duration=" << encoded->duration
                                            << " expected=" << expected_duration);
                    CHECK(encoded->duration >= expected_duration - 1);
                    CHECK(encoded->duration <= expected_duration + 1);
                }
                ++packets;
                REQUIRE(avcodec_send_packet(decoder, encoded) == 0);
                drain();
            }
            av_packet_unref(encoded);
        }
        REQUIRE(avcodec_send_packet(decoder, nullptr) == 0);
        drain();
        CHECK(packets == packets_per_loop * 3);
        CHECK(frames == packets_per_loop * 3);
        av_packet_free(&encoded);
        av_frame_free(&frame);
        avcodec_free_context(&decoder);
        avformat_close_input(&input);
    }
}

TEST_CASE("Private local source rejects a seam that would accumulate composition drift",
          "[media][local-loop][.private-local-reject]") {
    const char* source = std::getenv("COSMO_LOCAL_LOOP_TEST_SOURCE");
    if (!source || !*source)
        SKIP("Set COSMO_LOCAL_LOOP_TEST_SOURCE to an absolute private H264 source");
    REQUIRE(fs::path(source).is_absolute());
    cosmo::media::VideoDemuxer demux;
    demux.SetFile(source);
    REQUIRE(demux.OpenStream() == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream() == cosmo::util::ErrorEnum::Success);
    size_t count = 0;
    for (;;) {
        auto packet       = std::make_shared<cosmo::media::VideoPacket>();
        const auto result = demux.Demux(packet);
        if (result == cosmo::media::ReadFrameStatus::StreamEnd)
            break;
        REQUIRE(result == cosmo::media::ReadFrameStatus::Success);
        ++count;
    }
    REQUIRE(count > 1);
    REQUIRE(demux.CanContinueLocalLoop());
    demux.CloseStream(true);
    REQUIRE(demux.OpenStream(true) == cosmo::util::ErrorEnum::Success);
    REQUIRE(demux.FindStream(true) == cosmo::util::ErrorEnum::Success);
    auto packet = std::make_shared<cosmo::media::VideoPacket>();
    CHECK(demux.Demux(packet) == cosmo::media::ReadFrameStatus::UnsafeLocalContinuity);
}
