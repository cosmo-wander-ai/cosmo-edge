#include "media/VideoDecoder.h"
#include "media/VideoDecoderAxera.h"

namespace cosmo::media {

std::unique_ptr<VideoDecoder> VideoDecoder::Create(size_t name, void* media_handle) {
    static_cast<void>(media_handle);
    return std::make_unique<VideoDecoderAxera>(name);
}

VideoDecoderCapability VideoDecoder::Probe(VideoCodecType type) {
    return VideoDecoderAxera::Probe(type);
}

}  // namespace cosmo::media
