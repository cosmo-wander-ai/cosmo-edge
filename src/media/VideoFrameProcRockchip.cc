#include "media/VideoFrameProcRockchip.h"

#include <rga/im2d.h>

#include <chrono>
#include <limits>

#include "media/PreviewPipelineMetrics.h"
#include "media/RockchipRgaBuffer.h"
#include "util/Log.h"

namespace cosmo::media {
namespace {

    uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point started) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started)
                .count());
    }

}  // namespace

VideoFrameProcRockchip::VideoFrameProcRockchip(IOsdTextRenderer& osd_service)
    : VideoFrameProcCpu(osd_service) {}

VideoFramePtr VideoFrameProcRockchip::BGR2I420(VideoFramePtr frame) {
    auto converted = ConvertWithRga(frame, PixelFormat::PIXEL_I420, RK_FORMAT_BGR_888, RK_FORMAT_YCbCr_420_P,
                                    IM_RGB_TO_YUV_BT601_LIMIT, "BGR2I420");
    return converted ? converted : VideoFrameProcCpu::BGR2I420(std::move(frame));
}

VideoFramePtr VideoFrameProcRockchip::RGB2I420(VideoFramePtr frame) {
    auto converted = ConvertWithRga(frame, PixelFormat::PIXEL_I420, RK_FORMAT_RGB_888, RK_FORMAT_YCbCr_420_P,
                                    IM_RGB_TO_YUV_BT601_LIMIT, "RGB2I420");
    return converted ? converted : VideoFrameProcCpu::RGB2I420(std::move(frame));
}

VideoFramePtr VideoFrameProcRockchip::I4202BGR(VideoFramePtr frame) {
    auto converted = ConvertWithRga(frame, PixelFormat::PIXEL_BGR8, RK_FORMAT_YCbCr_420_P, RK_FORMAT_BGR_888,
                                    IM_YUV_TO_RGB_BT601_LIMIT, "I4202BGR");
    return converted ? converted : VideoFrameProcCpu::I4202BGR(std::move(frame));
}

VideoFramePtr VideoFrameProcRockchip::I4202RGB(VideoFramePtr frame) {
    auto converted = ConvertWithRga(frame, PixelFormat::PIXEL_RGB8, RK_FORMAT_YCbCr_420_P, RK_FORMAT_RGB_888,
                                    IM_YUV_TO_RGB_BT601_LIMIT, "I4202RGB");
    return converted ? converted : VideoFrameProcCpu::I4202RGB(std::move(frame));
}

VideoFramePtr VideoFrameProcRockchip::Resize(VideoFramePtr frame, int dst_height, int dst_width) {
    auto resized = ResizeWithRga(frame, dst_height, dst_width, "Resize");
    return resized ? resized : VideoFrameProcCpu::Resize(std::move(frame), dst_height, dst_width);
}

VideoFramePtr VideoFrameProcRockchip::ConvertWithRga(const VideoFramePtr& frame, PixelFormat dst_format,
                                                     int src_rga_format, int dst_rga_format, int color_mode,
                                                     const char* operation) {
    if (rga_unavailable_.load(std::memory_order_relaxed))
        return nullptr;
    if (!VideoFrameValid(frame, true) || frame->GetWidth() == 0 || frame->GetHeight() == 0 ||
        frame->GetWidth() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        frame->GetHeight() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return nullptr;
    }

    const int width  = static_cast<int>(frame->GetWidth());
    const int height = static_cast<int>(frame->GetHeight());
    auto output      = std::make_shared<VideoFrame>(width, height, dst_format, frame->GetFrameIndex(),
                                                    frame->GetTimestamp());
    if (!VideoFrameValid(output, true)) {
        return nullptr;
    }
    output->SetStreamIndex(frame->GetStreamIndex());

    const auto started = std::chrono::steady_clock::now();
    ScopedRgaBufferHandle src_handle(frame->GetData(), frame->GetSize());
    ScopedRgaBufferHandle dst_handle(output->GetData(), output->GetSize());
    if (src_handle.Get() == 0 || dst_handle.Get() == 0) {
        const auto elapsed = ElapsedNanoseconds(started);
        GetPreviewPipelineMetrics().RecordRgaOperation(false, elapsed);
        LogFallbackOnce(operation, IM_STATUS_OUT_OF_MEMORY);
        return nullptr;
    }

    auto src           = wrapbuffer_handle_t(src_handle.Get(), width, height, width, height, src_rga_format);
    auto dst           = wrapbuffer_handle_t(dst_handle.Get(), width, height, width, height, dst_rga_format);
    const auto status  = imcvtcolor_t(src, dst, src_rga_format, dst_rga_format, color_mode, 1);
    const auto elapsed = ElapsedNanoseconds(started);
    GetPreviewPipelineMetrics().RecordRgaOperation(RockchipRgaSucceeded(status), elapsed);
    if (!RockchipRgaSucceeded(status)) {
        LogFallbackOnce(operation, status);
        return nullptr;
    }
    return output;
}

VideoFramePtr VideoFrameProcRockchip::ResizeWithRga(const VideoFramePtr& frame, int dst_height, int dst_width,
                                                    const char* operation) {
    if (rga_unavailable_.load(std::memory_order_relaxed))
        return nullptr;
    if (!VideoFrameValid(frame, true) || frame->GetPixelFormat() != PixelFormat::PIXEL_I420 ||
        dst_width <= 0 || dst_height <= 0 ||
        frame->GetWidth() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        frame->GetHeight() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return nullptr;
    }

    const int src_width  = static_cast<int>(frame->GetWidth());
    const int src_height = static_cast<int>(frame->GetHeight());
    auto output          = std::make_shared<VideoFrame>(dst_width, dst_height, PixelFormat::PIXEL_I420,
                                                        frame->GetFrameIndex(), frame->GetTimestamp());
    if (!VideoFrameValid(output, true)) {
        return nullptr;
    }
    output->SetStreamIndex(frame->GetStreamIndex());

    const auto started = std::chrono::steady_clock::now();
    ScopedRgaBufferHandle src_handle(frame->GetData(), frame->GetSize());
    ScopedRgaBufferHandle dst_handle(output->GetData(), output->GetSize());
    if (src_handle.Get() == 0 || dst_handle.Get() == 0) {
        const auto elapsed = ElapsedNanoseconds(started);
        GetPreviewPipelineMetrics().RecordRgaOperation(false, elapsed);
        LogFallbackOnce(operation, IM_STATUS_OUT_OF_MEMORY);
        return nullptr;
    }

    auto src           = wrapbuffer_handle_t(src_handle.Get(), src_width, src_height, src_width, src_height,
                                             RK_FORMAT_YCbCr_420_P);
    auto dst           = wrapbuffer_handle_t(dst_handle.Get(), dst_width, dst_height, dst_width, dst_height,
                                             RK_FORMAT_YCbCr_420_P);
    const auto status  = imresize_t(src, dst, 0.0, 0.0, INTER_LINEAR, 1);
    const auto elapsed = ElapsedNanoseconds(started);
    GetPreviewPipelineMetrics().RecordRgaOperation(RockchipRgaSucceeded(status), elapsed);
    if (!RockchipRgaSucceeded(status)) {
        LogFallbackOnce(operation, status);
        return nullptr;
    }
    return output;
}

void VideoFrameProcRockchip::LogFallbackOnce(const char* operation, int status) {
    rga_unavailable_.store(true, std::memory_order_relaxed);
    if (!fallback_warning_logged_.test_and_set(std::memory_order_relaxed)) {
        LOG_WARN(
            "RGA {} failed with status {} ({}); disabling RGA for this frame processor and using CPU "
            "fallback",
            operation, status, imStrError_t(static_cast<IM_STATUS>(status)));
    }
}

}  // namespace cosmo::media
