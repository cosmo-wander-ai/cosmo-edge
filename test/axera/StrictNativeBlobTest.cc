#include <cassert>
#include <cstdlib>

#include "infer/AiComponment.h"
#include "util/Log.h"

namespace {
int get_data_calls = 0;
}
namespace cosmo::log {
size_t LogFormatArg(const char*, const char*, int, int, int, fmt::string_view, fmt::format_args) {
    return 0;
}
}  // namespace cosmo::log
// Minimal metadata-only VideoFrame used to exercise the real conversion entry.
namespace cosmo::media {
VideoFrame::VideoFrame(int w, int h, PixelFormat f, uint64_t i, int64_t ts, Materializer)
    : active_(true), width_(w), height_(h), pixel_fmt_(f), frame_index_(i), timestamp_(ts) {}
VideoFrame::~VideoFrame() = default;
bool VideoFrame::Active() const {
    return active_;
}
bool VideoFrame::IsDeferred() const {
    return true;
}
size_t VideoFrame::GetWidth() const {
    return width_;
}
size_t VideoFrame::GetHeight() const {
    return height_;
}
PixelFormat VideoFrame::GetPixelFormat() const {
    return pixel_fmt_;
}
uint8_t* VideoFrame::GetData() {
    ++get_data_calls;
    return nullptr;
}
}  // namespace cosmo::media
int main() {
    using namespace cosmo;
    unsetenv("COSMO_AXERA_STRICT_ZERO_COPY");
    auto image            = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_I420, 7, 8,
                                                                [] { return VideoFramePtr{}; });
    auto native           = std::make_shared<media::NativeVideoBuffer>();
    native->memory_kind   = media::NativeVideoMemoryKind::AxeraPhysical;
    native->bytes         = 24;
    native->width         = 4;
    native->height        = 4;
    native->width_stride  = 4;
    native->height_stride = 4;
    native->format        = media::NativeVideoBufferFormat::NV12;
    native->plane_count   = 2;
    native->planes[0].physical_address = 0x1000;
    native->planes[0].bytes            = 16;
    native->planes[0].stride           = 4;
    native->planes[1].physical_address = 0x2000;
    native->planes[1].bytes            = 8;
    native->planes[1].stride           = 4;
    native->backend_descriptor         = std::make_shared<media::NativeVideoBackendDescriptor>();
    native->owner                      = std::make_shared<int>(1);
    std::weak_ptr<const media::NativeVideoBuffer> weak = native;
    std::vector<VideoFramePtr> images{image};
    std::vector<media::NativeVideoBufferPtr> buffers{native};
    std::vector<std::shared_ptr<nn::Blob>> blobs;
    assert(ConvertImagesToBlobs(images, buffers, blobs) == util::ErrorEnum::Success);
    assert(blobs.size() == 1 && blobs[0]->GetHandle(false).axera_decoded_image);
    assert(!blobs[0]->GetHandle(false).base && get_data_calls == 0);
    buffers.clear();
    native.reset();
    assert(!weak.expired());
    blobs.clear();
    assert(weak.expired());
    assert(ConvertImagesToBlobs(images, buffers, blobs) != util::ErrorEnum::Success);
    assert(blobs.empty() && get_data_calls == 0);
}
