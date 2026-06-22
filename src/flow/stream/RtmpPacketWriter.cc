// RtmpPacketWriter — Rtmp Packet Writer implementation.

#include "flow/stream/RtmpPacketWriter.h"

#include "util/Log.h"

namespace cosmo {

namespace {
    std::string GetAvErr(int errorNo) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errorNo, buf, AV_ERROR_MAX_STRING_SIZE);
        return buf;
    }
}  // namespace

RtmpPacketWriter::RtmpPacketWriter(float fps) : fps_(fps) {}

int RtmpPacketWriter::WriteFrame(AVFormatContext* ctx, int stream_index, const uint8_t* data, size_t size,
                                 bool is_key_frame) {
    if (!ctx || !ctx->pb || stream_index < 0 || static_cast<unsigned>(stream_index) >= ctx->nb_streams) {
        return AVERROR(EINVAL);
    }

    // Zero-init replaces deprecated av_init_packet()
    AVPacket pkt{};

    if (is_key_frame) {
        pkt.flags |= AV_PKT_FLAG_KEY;
    }

    // SAFETY: FFmpeg AVPacket.data is non-const but av_interleaved_write_frame
    // does not mutate the buffer. const_cast required at C API boundary.
    pkt.data = const_cast<uint8_t*>(data);  // NOLINT: FFmpeg C API boundary
    pkt.size = static_cast<int>(size);

    pkt.dts      = count_;
    pkt.pts      = count_;
    pkt.duration = 1;
    ++count_;

    AVRational in_time_base = {1000, static_cast<int>(fps_ * 1000)};
    AVStream* s             = ctx->streams[stream_index];
    av_packet_rescale_ts(&pkt, in_time_base, s->time_base);

    pkt.stream_index = stream_index;

    int ret = av_interleaved_write_frame(ctx, &pkt);
    av_packet_unref(&pkt);  // replaces deprecated av_free_packet()

    if (ret < 0) {
        LOG_ERRO("av_interleaved_write_frame failed: [{}]", GetAvErr(ret));
    }

    return ret;
}

}  // namespace cosmo
