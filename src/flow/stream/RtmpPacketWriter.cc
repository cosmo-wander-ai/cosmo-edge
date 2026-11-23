// RtmpPacketWriter — Rtmp Packet Writer implementation.

#include "flow/stream/RtmpPacketWriter.h"

#include <cmath>
#include <limits>

#include "media/PacketTime.h"
#include "media/VideoPacket.h"
#include "util/Log.h"

namespace cosmo {

namespace {
    std::string GetAvErr(int errorNo) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errorNo, buf, AV_ERROR_MAX_STRING_SIZE);
        return buf;
    }
}  // namespace

RtmpPacketWriter::RtmpPacketWriter(float fps)
    : fps_(std::isfinite(fps) && fps > 0.0F && fps <= 240.0F ? fps : 25.0F) {}

void RtmpPacketWriter::ResetCounter() {
    count_                = 0;
    timing_mode_set_      = false;
    source_timing_        = false;
    origin_dts_           = 0;
    source_time_base_num_ = 0;
    source_time_base_den_ = 0;
    last_output_dts_      = AV_NOPTS_VALUE;
}

int RtmpPacketWriter::WriteFrame(AVFormatContext* ctx, int stream_index, const uint8_t* data, size_t size,
                                 bool is_key_frame, const media::VideoPacket* source) {
    if (!ctx || !ctx->pb || !data || size == 0 ||
        size > static_cast<size_t>(std::numeric_limits<int>::max()) || stream_index < 0 ||
        static_cast<unsigned>(stream_index) >= ctx->nb_streams) {
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

    AVStream* s                = ctx->streams[stream_index];
    const bool has_source_time = source && (source->time_base_num != 0 || source->time_base_den != 0);
    const bool complete_time =
        has_source_time && source->pts != AV_NOPTS_VALUE && source->dts != AV_NOPTS_VALUE;
    if (timing_mode_set_ && source_timing_ && !has_source_time) {
        LOG_WARN("{}", "RTMP packet lost or changed its source timing metadata");
        return AVERROR(EINVAL);
    }
    const bool use_source_time = timing_mode_set_ ? source_timing_ : complete_time;
    if (!timing_mode_set_ && has_source_time && !complete_time)
        LOG_WARN("{}", "RTMP source timestamps unavailable; using legacy synthetic CFR timing");
    AVRational in_time_base = {1000, static_cast<int>(fps_ * 1000)};
    if (use_source_time) {
        if (timing_mode_set_ && (source_time_base_num_ != source->time_base_num ||
                                 source_time_base_den_ != source->time_base_den)) {
            LOG_WARN("{}", "RTMP source time base changed without reconnect");
            return AVERROR(EINVAL);
        }
        media::PacketTime time;
        std::string reason;
        if (!media::RescalePacketTime(source->pts, source->dts, source->duration, source->time_base_num,
                                      source->time_base_den, timing_mode_set_ ? origin_dts_ : source->dts,
                                      90000, time, reason)) {
            LOG_WARN("RTMP invalid source timestamps: {}", reason);
            return AVERROR(EINVAL);
        }
        pkt.pts      = time.pts;
        pkt.dts      = time.dts;
        pkt.duration = time.duration;
        in_time_base = {1, 90000};
    } else {
        // Encoder callbacks without source timestamps keep their existing CFR clock.
        pkt.dts      = count_;
        pkt.pts      = count_;
        pkt.duration = 1;
    }
    av_packet_rescale_ts(&pkt, in_time_base, s->time_base);
    if ((last_output_dts_ != AV_NOPTS_VALUE && pkt.dts <= last_output_dts_) || pkt.pts < pkt.dts) {
        LOG_WARN("{}", "RTMP non-monotonic decode timestamp or negative composition offset");
        return AVERROR(EINVAL);
    }

    pkt.stream_index = stream_index;

    const int64_t output_dts = pkt.dts;
    int ret                  = av_interleaved_write_frame(ctx, &pkt);
    av_packet_unref(&pkt);  // replaces deprecated av_free_packet()

    if (ret < 0) {
        LOG_ERRO("av_interleaved_write_frame failed: [{}]", GetAvErr(ret));
    } else {
        if (!timing_mode_set_ && use_source_time) {
            origin_dts_           = source->dts;
            source_time_base_num_ = source->time_base_num;
            source_time_base_den_ = source->time_base_den;
        }
        timing_mode_set_ = true;
        source_timing_   = use_source_time;
        last_output_dts_ = output_dts;
        ++count_;
    }

    return ret;
}

}  // namespace cosmo
