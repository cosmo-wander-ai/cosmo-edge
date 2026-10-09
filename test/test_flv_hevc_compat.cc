#include <algorithm>
#include <cstring>
#include <vector>

#include "catch_amalgamated.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

namespace {
using Bytes = std::vector<uint8_t>;

void AppendBe(Bytes& out, uint32_t value, int count) {
    for (int i = count - 1; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

void AppendTag(Bytes& out, const Bytes& payload, uint32_t timestamp) {
    out.push_back(9);
    AppendBe(out, payload.size(), 3);
    AppendBe(out, timestamp, 3);
    out.push_back(static_cast<uint8_t>(timestamp >> 24));
    AppendBe(out, 0, 3);
    out.insert(out.end(), payload.begin(), payload.end());
    AppendBe(out, payload.size() + 11, 4);
}

// Synthetic container data only: no camera recordings or private streams.
struct MemoryFlv {
    Bytes data;
    size_t offset           = 0;
    AVFormatContext* format = nullptr;
    AVIOContext* io         = nullptr;

    explicit MemoryFlv(int codec) : data{'F', 'L', 'V', 1, 1, 0, 0, 0, 9, 0, 0, 0, 0} {
        Bytes config = codec == 12 ? Bytes(23, 0) : Bytes{1, 100, 0, 31, 255, 224, 0};
        config[0]    = 1;
        if (codec == 12) {
            config[21] = 3;  // hvcC: four-byte NAL lengths, zero parameter-set arrays.
        }
        Bytes header{static_cast<uint8_t>(0x10 | codec), 0, 0, 0, 0};
        header.insert(header.end(), config.begin(), config.end());
        AppendTag(data, header, 100);
        // Negative composition offset: DTS=100, PTS=98. Preserve the NAL length prefix.
        AppendTag(data,
                  Bytes{static_cast<uint8_t>(0x10 | codec), 1, 255, 255, 254, 0, 0, 0, 3, 0x26, 1, 0x80},
                  100);
        format       = avformat_alloc_context();
        auto* buffer = static_cast<unsigned char*>(av_malloc(4096));
        io           = avio_alloc_context(buffer, 4096, 0, this, Read, nullptr, nullptr);
        if (!io) {
            av_free(buffer);
        }
        if (format) {
            format->pb = io;
            format->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_NOPARSE | AVFMT_FLAG_NOFILLIN;
        }
    }

    ~MemoryFlv() {
        avformat_close_input(&format);
        if (io) {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
    }

    static int Read(void* opaque, uint8_t* output, int size) {
        auto& self       = *static_cast<MemoryFlv*>(opaque);
        const auto count = std::min(self.data.size() - self.offset, static_cast<size_t>(size));
        if (count == 0) {
            return AVERROR_EOF;
        }
        std::memcpy(output, self.data.data() + self.offset, count);
        self.offset += count;
        return static_cast<int>(count);
    }
};
}  // namespace

TEST_CASE("FLV legacy HEVC and AVC preserve codec configuration and packet timing", "[media][flv]") {
    const int codec = GENERATE(7, 12);
    CAPTURE(codec);
    MemoryFlv input(codec);
    REQUIRE(input.format != nullptr);
    REQUIRE(input.io != nullptr);
    REQUIRE(avformat_open_input(&input.format, nullptr, av_find_input_format("flv"), nullptr) == 0);
    AVPacket* packet = av_packet_alloc();
    REQUIRE(packet != nullptr);
    const int result = av_read_frame(input.format, packet);
    // Release before assertions so failing regression runs do not leak packet storage.
    const auto size  = packet->size;
    const auto pts   = packet->pts;
    const auto dts   = packet->dts;
    const auto flags = packet->flags;
    Bytes payload;
    if (packet->data && packet->size > 0) {
        payload.assign(packet->data, packet->data + packet->size);
    }
    av_packet_free(&packet);
    REQUIRE(result == 0);
    REQUIRE(input.format->nb_streams == 1);
    const auto* parameters = input.format->streams[0]->codecpar;
    CHECK(parameters->codec_id == (codec == 12 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264));
    CHECK(parameters->extradata_size == (codec == 12 ? 23 : 7));
    CHECK(size == 7);
    CHECK(payload == Bytes{0, 0, 0, 3, 0x26, 1, 0x80});
    CHECK(dts == 100);
    CHECK(pts == 98);
    CHECK((flags & AV_PKT_FLAG_KEY) != 0);
}
