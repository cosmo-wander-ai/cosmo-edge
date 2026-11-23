#include "media/VideoDecoder.h"
#include "media/VideoDecoderRockchip.h"

namespace cosmo::media {

std::unique_ptr<VideoDecoder> VideoDecoder::Create(size_t name, void* media_handle) {
    static_cast<void>(media_handle);
    return std::make_unique<VideoDecoderRockchip>(name);
}

VideoDecoderCapability VideoDecoder::Probe(VideoCodecType type) {
    return VideoDecoderRockchip::ProbeCapability(type);
}

}  // namespace cosmo::media
