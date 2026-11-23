#pragma once
#include <functional>
#include <memory>

#include "media/AxeraVideoFrameCopy.h"
#include "media/VideoFrame.h"
#include "nn/device/axera/benchmark/BenchHostNv12Snapshot.h"
namespace cosmo::nn::axera::benchmark {
// Historical 2f3b policy: hold this exact native lease until a consumer
// requests pixels or the VideoFrame is destroyed. No background timer/copy.
inline std::shared_ptr<media::VideoFrame> MakeBorrowedEventFrame(
    const media::NativeVideoBufferPtr& native, const std::shared_ptr<media::VideoFrame>& metadata,
    const std::string& channel, const std::shared_ptr<SnapshotMap>& mapper,
    std::function<void(SnapshotError)> finished) {
    if (!native || !metadata || !metadata->Active() || !mapper || channel.empty() || !native->Valid() ||
        native->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        native->format != media::NativeVideoBufferFormat::NV12 ||
        native->color_space != media::NativeVideoColorSpace::Bt709 ||
        native->color_range != media::NativeVideoColorRange::Limited || native->width != 1920 ||
        native->height != 1080 || native->width_stride != 2048 || native->height_stride != 1080 ||
        native->crop_x || native->crop_y || native->planes[0].stride != 2048 ||
        native->planes[1].stride != 2048 || native->planes[0].bytes != 2211840 ||
        native->planes[1].bytes != 1105920 || native->bytes < 3317760 || !native->frame_witness ||
        !native->frame_witness->Matches(*native) || native->frame_index != metadata->GetFrameIndex() ||
        native->source_timestamp != metadata->GetTimestamp() ||
        native->stream_generation != static_cast<std::uint64_t>(metadata->GetStreamIndex()))
        return {};
    const auto index     = metadata->GetFrameIndex();
    const auto timestamp = metadata->GetTimestamp();
    const auto stream    = metadata->GetStreamIndex();
    const SnapshotKey key{channel, native->stream_generation, native->frame_index, native->source_timestamp};
    auto provider = [native, key, mapper, finished, index, timestamp,
                     stream]() -> std::shared_ptr<media::VideoFrame> {
        try {
            auto budget = std::make_shared<SnapshotBudget>(1);
            auto result = CopyValidated(native, key, *mapper, budget);
            if (!result.snapshot) {
                if (finished)
                    finished(result.error);
                return {};
            }
            const auto& host = *result.snapshot;
            auto out         = std::make_shared<media::VideoFrame>(host.Width(), host.Height(),
                                                                   media::PixelFormat::PIXEL_I420, index, timestamp);
            if (!out->Active() ||
                !media::CopyAxeraSemiplanarToI420(
                    host.Y().data(), host.Y().size(), host.YStride(), host.Uv().data(), host.Uv().size(),
                    host.UvStride(), host.Width(), host.Height(), false, out->GetData(), out->GetSize())) {
                if (finished)
                    finished(SnapshotError::CopyFailed);
                return {};
            }
            out->SetStreamIndex(stream);
            if (finished)
                finished(SnapshotError::None);
            return out;
        } catch (...) {
            if (finished)
                finished(SnapshotError::CopyFailed);
            return {};
        }
    };
    auto out = std::make_shared<media::VideoFrame>(
        native->width, native->height, media::PixelFormat::PIXEL_I420, index, timestamp, std::move(provider));
    if (!out->Active())
        return {};
    out->SetStreamIndex(stream);
    return out;
}
}  // namespace cosmo::nn::axera::benchmark
