#pragma once
#include <cmath>
extern "C" {
#include "libavformat/avformat.h"
}
namespace cosmo::media {
// Pure metadata admission. Native packet/PTS/VUI/frame witnesses remain
// authoritative; these container fields never establish decoded color.
inline bool NativeDemuxForcedFpsReady(float fps) {
    return std::isfinite(fps) && (fps == -1.0f || (fps >= 0.0f && fps <= 1000.0f));
}
inline bool NativeDemuxFpsMetadataReady(const AVStream* s) {
    if (!s)
        return false;
    const auto a = s->avg_frame_rate;
    const auto b = s->r_frame_rate;
    return (a.num > 0 && a.den > 0) || (b.num > 0 && b.den > 0);
}
inline bool NativeDemuxMetadataReady(const AVFormatContext* ctx, int stream_index, bool requires_extradata) {
    if (!ctx || stream_index < 0 || static_cast<unsigned>(stream_index) >= ctx->nb_streams || !ctx->streams ||
        !ctx->streams[stream_index])
        return false;
    const AVStream* stream     = ctx->streams[stream_index];
    const AVCodecParameters* c = stream->codecpar;
    return c && c->codec_type == AVMEDIA_TYPE_VIDEO && c->codec_id == AV_CODEC_ID_H264 && c->width > 0 &&
           c->height > 0 && !(c->width & 1) && !(c->height & 1) && stream->time_base.num > 0 &&
           stream->time_base.den > 0 && (!requires_extradata || (c->extradata && c->extradata_size > 0));
}
}  // namespace cosmo::media
