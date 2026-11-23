#pragma once
#include <memory>

#include "media/AxeraVideoFrameCopy.h"
#include "media/VideoFrame.h"
#include "nn/device/axera/benchmark/BenchHostNv12Snapshot.h"

namespace cosmo::nn::axera::benchmark {
struct LazyEventResult {
    std::shared_ptr<media::VideoFrame> frame;
    SnapshotError error{SnapshotError::InvalidFrame};
    std::unique_ptr<SnapshotQuarantine> quarantine;
};
// A successful deferred frame owns only an independent host NV12 copy. It
// never holds a VDEC frame lease across the asynchronous task graph.
inline LazyEventResult MakeLazyEventFrame(const media::NativeVideoBufferPtr& native,
                                          const std::shared_ptr<media::VideoFrame>& metadata,
                                          const std::string& channel, SnapshotMap& mapper,
                                          const std::shared_ptr<SnapshotBudget>& budget) {
    LazyEventResult result;
    if (!native || !metadata || !metadata->Active() || native->frame_index != metadata->GetFrameIndex() ||
        native->source_timestamp != metadata->GetTimestamp() ||
        native->stream_generation != static_cast<std::uint64_t>(metadata->GetStreamIndex()))
        return result;
    SnapshotKey key{channel, native->stream_generation, native->frame_index, native->source_timestamp};
    auto copied       = CopyValidated(native, key, mapper, budget);
    result.error      = copied.error;
    result.quarantine = std::move(copied.quarantine);
    if (!copied.snapshot)
        return result;
    auto host            = std::move(copied.snapshot);
    const auto index     = metadata->GetFrameIndex();
    const auto timestamp = metadata->GetTimestamp();
    const auto stream    = metadata->GetStreamIndex();
    auto provider        = [host, index, timestamp, stream]() -> std::shared_ptr<media::VideoFrame> {
        auto out = std::make_shared<media::VideoFrame>(host->Width(), host->Height(),
                                                              media::PixelFormat::PIXEL_I420, index, timestamp);
        if (!out->Active() ||
            !media::CopyAxeraSemiplanarToI420(
                host->Y().data(), host->Y().size(), host->YStride(), host->Uv().data(), host->Uv().size(),
                host->UvStride(), host->Width(), host->Height(), false, out->GetData(), out->GetSize()))
            return {};
        out->SetStreamIndex(stream);
        return out;
    };
    result.frame = std::make_shared<media::VideoFrame>(
        host->Width(), host->Height(), media::PixelFormat::PIXEL_I420, index, timestamp, std::move(provider));
    if (!result.frame->Active()) {
        result.frame.reset();
        result.error = SnapshotError::CopyFailed;
        return result;
    }
    result.frame->SetStreamIndex(stream);
    return result;
}
}  // namespace cosmo::nn::axera::benchmark
