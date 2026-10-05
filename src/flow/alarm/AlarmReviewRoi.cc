#include "flow/alarm/AlarmReviewRoi.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "media/PixelFormatUtils.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"

namespace cosmo {
AlarmReviewRoi PrepareAlarmReviewRoi(const VideoFramePtr& frame, const util::Box& box) {
    AlarmReviewRoi result;
    if (!VideoFrameValid(frame))
        return result;
    auto srcFrame =
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(frame);
    if (!VideoFrameValid(srcFrame))
        return result;
    result.sourceWidth  = static_cast<int>(srcFrame->GetWidth());
    result.sourceHeight = static_cast<int>(srcFrame->GetHeight());
    result.requested    = AlarmReviewCropBox(result.sourceWidth, result.sourceHeight, box);
    const auto width    = result.requested.width;
    const auto height   = result.requested.height;
    auto inputFrame     = srcFrame;
    result.actual       = {0, 0, result.sourceWidth, result.sourceHeight};
    result.mode         = "full_frame_fallback";
    if (width > 0 && height > 0) {
        if (width < result.sourceWidth || height < result.sourceHeight) {
            auto cropped = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(
                srcFrame, result.requested);
            if (VideoFrameValid(cropped)) {
                inputFrame = cropped;
                // Match the backend's even-pixel YUV alignment and minimum VPP crop size.
#ifdef COSMO_MEDIA_USE_SOPHON_BACKEND
                constexpr int minDimension = 16;
                constexpr int maxDimension = 8192;
#else
                constexpr int minDimension = 1;
                constexpr int maxDimension = std::numeric_limits<int>::max();
#endif
                auto actual = media::PixelFormatUtils::NormalizeCropRoi(
                    result.sourceWidth, result.sourceHeight, srcFrame->GetPixelFormat(), result.requested,
                    minDimension, maxDimension);
                if (actual && actual->width == static_cast<int>(cropped->GetWidth()) &&
                    actual->height == static_cast<int>(cropped->GetHeight())) {
                    result.actual = *actual;
                    result.mode   = "cropped";
                } else {
                    result.actual = {};
                    result.mode   = "cropped_bounds_unverified";
                }
            }
        } else
            result.mode = "full_frame";
    }
    if (!VideoFrameValid(inputFrame))
        return result;
    auto& transform   = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
    const auto format = inputFrame->GetPixelFormat();
    if (format == media::PixelFormat::PIXEL_I420)
        inputFrame = transform.I4202BGR(inputFrame);
    else if (format != media::PixelFormat::PIXEL_BGR8 && format != media::PixelFormat::PIXEL_RGB8)
        return result;
    if (!VideoFrameValid(inputFrame))
        return result;
    inputFrame->SetFrameIndex(frame->GetFrameIndex());
    inputFrame->SetTimestamp(frame->GetTimestamp());
    inputFrame->SetStreamIndex(frame->GetStreamIndex());
    bool hasHost = transform.EnsureHostData(inputFrame) && inputFrame->GetHostData();
    if (!hasHost && !inputFrame->GetData())
        return result;
    result.frame = std::move(inputFrame);
    return result;
}
}  // namespace cosmo
