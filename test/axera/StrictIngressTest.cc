#include <cassert>
#include <limits>

#include "media/NativeDemuxAdmission.h"

int main() {
    using namespace cosmo::media;
    AVCodecParameters codec{};
    codec.codec_type = AVMEDIA_TYPE_VIDEO;
    codec.codec_id   = AV_CODEC_ID_H264;
    codec.width      = 1920;
    codec.height     = 1080;
    AVStream stream{};
    stream.codecpar  = &codec;
    stream.time_base = {1, 90000};
    AVStream* streams[]{&stream};
    AVFormatContext context{};
    context.nb_streams = 1;
    context.streams    = streams;

    assert(NativeDemuxMetadataReady(&context, 0, false));
    assert(!NativeDemuxMetadataReady(&context, 0, true));
    unsigned char parameter_set{};
    codec.extradata      = &parameter_set;
    codec.extradata_size = 1;
    assert(NativeDemuxMetadataReady(&context, 0, true));
    codec.width = 1921;
    assert(!NativeDemuxMetadataReady(&context, 0, true));
    codec.width    = 1920;
    codec.codec_id = AV_CODEC_ID_HEVC;
    assert(!NativeDemuxMetadataReady(&context, 0, true));
    assert(!NativeDemuxMetadataReady(&context, 1, true));

    assert(!NativeDemuxFpsMetadataReady(&stream));
    stream.r_frame_rate = {24000, 1001};
    assert(NativeDemuxFpsMetadataReady(&stream));
    assert(NativeDemuxForcedFpsReady(-1.0f));
    assert(NativeDemuxForcedFpsReady(5.0f));
    assert(!NativeDemuxForcedFpsReady(std::numeric_limits<float>::quiet_NaN()));
}
