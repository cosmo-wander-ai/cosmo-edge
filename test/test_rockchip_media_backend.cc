#include "catch_amalgamated.hpp"

#ifdef COSMO_MEDIA_USE_ROCKCHIP_BACKEND

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "media/IOsdTextRenderer.h"
#include "media/PixelFormat.h"
#include "media/PreviewPipelineMetrics.h"
#include "media/VideoDecoder.h"
#include "media/VideoEncoder.h"
#include "media/VideoFrame.h"
#include "media/VideoFrameProcRockchip.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#if defined(COSMO_NN_USE_RKNN_BACKEND)
#include "nn/core/blob.h"
#include "nn/core/inference_pipeline_metrics.h"
#include "nn/core/shared_resource.h"
#include "nn/device/rknn/rknn_preprocess_node.h"
#include "nn/utils/op.h"
#endif

namespace {

class StubOsdTextRenderer final : public cosmo::media::IOsdTextRenderer {
public:
    bool Init(const std::string&) override {
        return true;
    }
    bool IsReady() const override {
        return false;
    }
    TextBitmap RenderString(const std::string&, float) const override {
        return {};
    }
    OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
        return {};
    }
};

bool HasAnnexBStartCode(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 0 && data[1] == 0 &&
           ((data[2] == 1) || (data[2] == 0 && data[3] == 1));
}

size_t CountSelfDmaBufFds() {
    std::error_code error;
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fdinfo", error)) {
        if (error)
            break;
        std::ifstream input(entry.path());
        std::string line;
        while (std::getline(input, line)) {
            if (line.rfind("exp_name:", 0) == 0) {
                ++count;
                break;
            }
        }
    }
    return count;
}

#if defined(COSMO_NN_USE_RKNN_BACKEND)
class ScopedEnvValue {
public:
    ScopedEnvValue(const char* name, const char* value) : name_(name) {
        if (const char* current = std::getenv(name))
            previous_ = current;
        setenv(name, value, 1);
    }
    ~ScopedEnvValue() {
        if (previous_)
            setenv(name_.c_str(), previous_->c_str(), 1);
        else
            unsetenv(name_.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};
#endif

}  // namespace

TEST_CASE("Rockchip MPP encodes compact I420 as Annex-B H264", "[media][rockchip][encoder][.device]") {
    constexpr int width  = 640;
    constexpr int height = 360;
    std::vector<uint8_t> i420(static_cast<size_t>(width) * height * 3 / 2, 128);
    std::fill_n(i420.begin(), static_cast<size_t>(width) * height, 32);

    auto encoder = cosmo::media::VideoEncoder::Create(nullptr);
    REQUIRE(encoder);
    encoder->Set(cosmo::media::VideoCodecType::kH264, width, height);
    REQUIRE(encoder->Open());

    auto packet = encoder->SendYUVFrame(i420.data());
    REQUIRE(packet);
    CHECK(packet->GetSize() > 0);
    CHECK(packet->IsIFrame());
    CHECK(HasAnnexBStartCode(packet->data));
}

TEST_CASE("Rockchip RGA performs admitted host-buffer conversions and resize",
          "[media][rockchip][rga][.device]") {
    constexpr int width     = 64;
    constexpr int height    = 64;
    constexpr int pool_size = width * height * 3;
    cosmo::mem::MemoryPoolMng memory_pool(std::make_unique<cosmo::mem::AllocatorCpu>(), {pool_size});
    cosmo::mem::SetMemoryPoolContext(&memory_pool);
    struct PoolReset {
        ~PoolReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } pool_reset;

    const auto before = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    {
        StubOsdTextRenderer osd;
        cosmo::media::VideoFrameProcRockchip processor(osd);
        auto bgr =
            std::make_shared<cosmo::media::VideoFrame>(width, height, cosmo::media::PixelFormat::PIXEL_BGR8);
        REQUIRE(VideoFrameValid(bgr, true));
        std::fill_n(bgr->GetData(), bgr->GetSize(), 96);

        auto i420 = processor.BGR2I420(bgr);
        REQUIRE(VideoFrameValid(i420, true));
        CHECK(i420->GetPixelFormat() == cosmo::media::PixelFormat::PIXEL_I420);

        auto resized = processor.Resize(i420, height / 2, width / 2);
        REQUIRE(VideoFrameValid(resized, true));
        CHECK(resized->GetWidth() == width / 2);
        CHECK(resized->GetHeight() == height / 2);

        auto rgb = processor.I4202RGB(i420);
        REQUIRE(VideoFrameValid(rgb, true));
        CHECK(rgb->GetPixelFormat() == cosmo::media::PixelFormat::PIXEL_RGB8);
    }

    const auto after = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    CHECK(after.rga_frames - before.rga_frames == 3);
    CHECK(after.rga_failures - before.rga_failures == 0);
}

TEST_CASE("Rockchip MPP decodes its H264 output through the Copy-out boundary",
          "[media][rockchip][decoder][.device]") {
    constexpr int width     = 640;
    constexpr int height    = 360;
    constexpr int pool_size = width * height * 3 / 2;
    constexpr int bgr_size  = width * height * 3;
    cosmo::mem::MemoryPoolMng memory_pool(std::make_unique<cosmo::mem::AllocatorCpu>(),
                                          {pool_size, bgr_size});
    cosmo::mem::SetMemoryPoolContext(&memory_pool);
    struct PoolReset {
        ~PoolReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } pool_reset;

    std::vector<uint8_t> i420(static_cast<size_t>(pool_size), 128);
    std::fill_n(i420.begin(), static_cast<size_t>(width) * height, 48);

    auto encoder = cosmo::media::VideoEncoder::Create(nullptr);
    REQUIRE(encoder);
    encoder->Set(cosmo::media::VideoCodecType::kH264, width, height);
    REQUIRE(encoder->Open());

    std::vector<cosmo::media::VideoPacketPtr> packets;
    for (int index = 0; index < 16; ++index) {
        auto packet = encoder->SendYUVFrame(i420.data());
        REQUIRE(packet);
        packets.push_back(std::move(packet));
    }

    auto decoder = cosmo::media::VideoDecoder::Create(0, nullptr);
    REQUIRE(decoder);
    decoder->SetCodecType(cosmo::media::VideoCodecType::kH264, width, height);
    REQUIRE(decoder->Open());

    const auto before = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    cosmo::media::VideoFramePtr decoded;
    cosmo::media::NativeVideoBufferPtr native_buffer;
    bool discarded_deferred_output = false;
    for (size_t index = 0; index < packets.size(); ++index) {
        bool accepted = false;
        auto output   = decoder->DecodeFrame(packets[index]->data.data(), packets[index]->data.size(),
                                             static_cast<int64_t>(index + 1), accepted);
        CHECK(accepted);
        if (output.IsDeferred() && !discarded_deferred_output) {
            output.Discard();
            discarded_deferred_output = true;
            continue;
        }
        if (output.HasFrame()) {
            native_buffer = output.ExportNativeBuffer();
            decoded       = output.Materialize();
        }
        if (decoded) {
            break;
        }
    }
    for (int attempt = 0; !decoded && attempt < 8; ++attempt) {
        decoded = decoder->GetFrame();
    }

    REQUIRE(decoded);
    CHECK(decoded->GetWidth() == width);
    CHECK(decoded->GetHeight() == height);
    CHECK(decoded->GetPixelFormat() == cosmo::media::PixelFormat::PIXEL_I420);
    CHECK(decoded->Active());
    REQUIRE(native_buffer);
    CHECK(native_buffer->Valid());
    CHECK(native_buffer->format == cosmo::media::NativeVideoBufferFormat::NV12);
    CHECK(native_buffer->width == width);
    CHECK(native_buffer->height == height);
    CHECK(native_buffer->width_stride >= width);
    CHECK(native_buffer->height_stride >= height);

#if defined(COSMO_NN_USE_RKNN_BACKEND)
    StubOsdTextRenderer osd;
    cosmo::media::VideoFrameProcRockchip processor(osd);
    auto bgr = processor.I4202BGR(decoded);
    REQUIRE(VideoFrameValid(bgr, true));

    cosmo::nn::BlobDesc source_desc;
    source_desc.device_type  = cosmo::nn::DEVICE_NAIVE;
    source_desc.data_type    = cosmo::nn::DATA_TYPE_UINT8;
    source_desc.data_format  = cosmo::nn::DATA_FORMAT_NHWC;
    source_desc.image_format = cosmo::nn::IMAGE_BGR;
    source_desc.dims         = {1, height, width, 3};
    cosmo::nn::BlobHandle source_handle;
    source_handle.base                       = bgr->GetData();
    source_handle.native_image.fd            = native_buffer->fd;
    source_handle.native_image.bytes         = native_buffer->bytes;
    source_handle.native_image.width         = native_buffer->width;
    source_handle.native_image.height        = native_buffer->height;
    source_handle.native_image.width_stride  = native_buffer->width_stride;
    source_handle.native_image.height_stride = native_buffer->height_stride;
    source_handle.native_image.format        = cosmo::nn::IMAGE_NV12;
    auto source_blob                         = std::make_shared<cosmo::nn::Blob>(source_desc, source_handle);

    cosmo::nn::Resize resize;
    resize.dsize   = {640, 640};
    resize.gravity = 1;
    resize.color   = {114, 114, 114};
    cosmo::nn::SharedResource resource;
    cosmo::nn::RknnResizeNode resize_node;
    resize_node.SetSharedResource(&resource);
    resize_node.LoadParam(&resize);
    REQUIRE(bool(resize_node.InferTopShapes()));
    cosmo::nn::BlobDesc target_desc;
    target_desc.device_type  = cosmo::nn::DEVICE_NAIVE;
    target_desc.data_type    = cosmo::nn::DATA_TYPE_UINT8;
    target_desc.data_format  = cosmo::nn::DATA_FORMAT_NHWC;
    target_desc.image_format = cosmo::nn::IMAGE_RGB;
    target_desc.dims         = {1, 640, 640, 3};
    auto target              = std::make_shared<cosmo::nn::Blob>(target_desc, true);
    REQUIRE(target->GetHandle().base);
    std::vector<std::shared_ptr<cosmo::nn::Blob>> bottoms{source_blob};
    std::vector<std::shared_ptr<cosmo::nn::Blob>> tops{target};

    std::vector<uint8_t> host_result(640 * 640 * 3);
    {
        ScopedEnvValue disabled("COSMO_RKNN_MPP_DMABUF", "0");
        REQUIRE(bool(resize_node.Forward(bottoms, tops)));
        std::copy_n(static_cast<const uint8_t*>(target->GetHandle().base), host_result.size(),
                    host_result.begin());
    }
    const auto inference_before      = cosmo::nn::GetInferencePipelineMetrics().Snapshot();
    int host_requests                = 0;
    source_handle.base               = nullptr;
    source_handle.host_data_provider = [bgr, &host_requests]() -> void* {
        ++host_requests;
        return bgr->GetData();
    };
    source_blob->SetHandle(source_handle);
    {
        ScopedEnvValue enabled("COSMO_RKNN_MPP_DMABUF", "1");
        REQUIRE(bool(resize_node.Forward(bottoms, tops)));
    }
    const auto inference_after = cosmo::nn::GetInferencePipelineMetrics().Snapshot();
    CHECK(host_requests == 0);
    const auto* native_result   = static_cast<const uint8_t*>(target->GetHandle().base);
    uint64_t absolute_error_sum = 0;
    int max_absolute_error      = 0;
    for (size_t index = 0; index < host_result.size(); ++index) {
        const int error = std::abs(static_cast<int>(host_result[index]) - native_result[index]);
        absolute_error_sum += static_cast<uint64_t>(error);
        max_absolute_error = std::max(max_absolute_error, error);
    }
    const double mean_absolute_error =
        static_cast<double>(absolute_error_sum) / static_cast<double>(host_result.size());
    CHECK(mean_absolute_error <= 2.0);
    CHECK(max_absolute_error <= 12);
    CHECK(inference_after.rknn_mpp_dmabuf_frames == inference_before.rknn_mpp_dmabuf_frames + 1);
    CHECK(inference_after.rknn_mpp_dmabuf_import_failures ==
          inference_before.rknn_mpp_dmabuf_import_failures);

    const auto fallback_before = cosmo::nn::GetInferencePipelineMetrics().Snapshot();
    {
        ScopedEnvValue enabled("COSMO_RKNN_MPP_DMABUF", "1");
        ScopedEnvValue forced("COSMO_RKNN_MPP_DMABUF_FORCE_FAIL", "1");
        REQUIRE(bool(resize_node.Forward(bottoms, tops)));
    }
    const auto fallback_after = cosmo::nn::GetInferencePipelineMetrics().Snapshot();
    CHECK(host_requests > 0);
    CHECK(std::equal(host_result.begin(), host_result.end(),
                     static_cast<const uint8_t*>(target->GetHandle().base)));
    CHECK(fallback_after.rknn_mpp_dmabuf_fallbacks == fallback_before.rknn_mpp_dmabuf_fallbacks + 1);
#endif

    const auto after = cosmo::media::GetPreviewPipelineMetrics().Snapshot();
    CHECK(discarded_deferred_output);
    CHECK(after.mpp_decoded_frames > before.mpp_decoded_frames);
    CHECK(after.mpp_decode_failures == before.mpp_decode_failures);
    CHECK(after.mpp_decode_fallbacks == before.mpp_decode_fallbacks);
    CHECK(after.mpp_copy_out_frames > before.mpp_copy_out_frames);
    CHECK(after.mpp_copy_out_failures == before.mpp_copy_out_failures);
    CHECK(after.mpp_early_dropped_frames == before.mpp_early_dropped_frames + 1);
    native_buffer.reset();
    decoded.reset();
    CHECK(decoder->Close());

    // Local loop playback changes streamIndex for every pass. Keep a compatible
    // RK3576 MPP context alive while exporting its DMA-BUF through RGA, then
    // require the warmed frame-group FD count to remain bounded.
    REQUIRE(decoder->Open());
    CHECK_FALSE(decoder->ReuseForStreamRestart(cosmo::media::VideoCodecType::kH264, width + 2, height));
    CHECK_FALSE(decoder->ReuseForStreamRestart(cosmo::media::VideoCodecType::kH265, width, height));
    size_t dmabuf_fds_after_warmup = 0;
    size_t reused_stream_frames    = 0;
    for (int cycle = 0; cycle < 17; ++cycle) {
        if (cycle > 0) {
            REQUIRE(decoder->ReuseForStreamRestart(cosmo::media::VideoCodecType::kH264, width, height));
        }
        bool got_frame = false;
        for (size_t packet_index = 0; packet_index < packets.size() && !got_frame; ++packet_index) {
            bool accepted = false;
            auto output =
                decoder->DecodeFrame(packets[packet_index]->data.data(), packets[packet_index]->data.size(),
                                     1000 + cycle * 100 + packet_index, accepted);
            REQUIRE(accepted);
            if (!output.HasFrame())
                continue;
            auto replay_native = output.ExportNativeBuffer();
            auto replay_frame  = output.Materialize();
            REQUIRE(replay_native);
            REQUIRE(replay_frame);
#if defined(COSMO_NN_USE_RKNN_BACKEND)
            auto replay_bgr = processor.I4202BGR(replay_frame);
            REQUIRE(VideoFrameValid(replay_bgr, true));
            auto replay_handle                       = source_blob->GetHandle();
            replay_handle.base                       = replay_bgr->GetData();
            replay_handle.host_data_provider         = {};
            replay_handle.native_image.fd            = replay_native->fd;
            replay_handle.native_image.bytes         = replay_native->bytes;
            replay_handle.native_image.width         = replay_native->width;
            replay_handle.native_image.height        = replay_native->height;
            replay_handle.native_image.width_stride  = replay_native->width_stride;
            replay_handle.native_image.height_stride = replay_native->height_stride;
            replay_handle.native_image.format        = cosmo::nn::IMAGE_NV12;
            source_blob->SetHandle(replay_handle);
            {
                ScopedEnvValue enabled("COSMO_RKNN_MPP_DMABUF", "1");
                REQUIRE(bool(resize_node.Forward(bottoms, tops)));
            }
#endif
            ++reused_stream_frames;
            got_frame = true;
        }
        REQUIRE(got_frame);
        if (cycle == 0) {
            dmabuf_fds_after_warmup = CountSelfDmaBufFds();
            REQUIRE(dmabuf_fds_after_warmup > 0);
        }
    }
    REQUIRE(reused_stream_frames == 17);
    const auto dmabuf_fds_after_reuse = CountSelfDmaBufFds();
    INFO("DMA-BUF FDs after warmup=" << dmabuf_fds_after_warmup
                                     << " after stream reuse=" << dmabuf_fds_after_reuse);
    CHECK(dmabuf_fds_after_reuse <= dmabuf_fds_after_warmup + 4);
    CHECK(decoder->Close());
}

#endif
