#include "flow/alarm/AlarmReviewRoi.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "media/PixelFormatUtils.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"

namespace cosmo {
namespace {
    AlarmReviewRoi Prepare(const VideoFramePtr& frame, const util::Box& box, bool strict,
                           AlarmReviewRoi& result) {
        result.failure = "invalid_source_frame";
        if (!VideoFrameValid(frame))
            return result;
        result.failure = "source_copy_failed";
        auto srcFrame =
            service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(frame);
        if (!VideoFrameValid(srcFrame))
            return result;
        result.failure = "source_geometry_mismatch";
        if (strict &&
            (srcFrame->GetWidth() != frame->GetWidth() || srcFrame->GetHeight() != frame->GetHeight() ||
             srcFrame->GetWidth() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
             srcFrame->GetHeight() > static_cast<size_t>(std::numeric_limits<int>::max())))
            return result;
        result.sourceWidth  = static_cast<int>(srcFrame->GetWidth());
        result.sourceHeight = static_cast<int>(srcFrame->GetHeight());
        result.failure      = "invalid_target_roi";
        if (strict && (box.width <= 0 || box.height <= 0 || box.x >= result.sourceWidth ||
                       box.y >= result.sourceHeight || static_cast<int64_t>(box.x) + box.width <= 0 ||
                       static_cast<int64_t>(box.y) + box.height <= 0))
            return result;
        result.requested  = AlarmReviewCropBox(result.sourceWidth, result.sourceHeight, box);
        const auto width  = result.requested.width;
        const auto height = result.requested.height;
        if (strict && (width <= 0 || height <= 0))
            return result;
        auto inputFrame = srcFrame;
        result.actual   = strict ? util::Box{} : util::Box{0, 0, result.sourceWidth, result.sourceHeight};
        result.mode     = strict ? "unprepared" : "full_frame_fallback";
        if (width > 0 && height > 0) {
            if (width < result.sourceWidth || height < result.sourceHeight) {
                result.failure = "roi_crop_failed";
                auto cropped = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Crop(
                    srcFrame, result.requested);
                if (strict && !VideoFrameValid(cropped))
                    return result;
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
                        result.actual  = {};
                        result.mode    = "cropped_bounds_unverified";
                        result.failure = "roi_bounds_unverified";
                        if (strict)
                            return result;
                    }
                }
            } else {
                result.actual = {0, 0, result.sourceWidth, result.sourceHeight};
                result.mode   = "full_frame";
            }
        }
        result.failure = "roi_frame_invalid";
        if (!VideoFrameValid(inputFrame))
            return result;
        auto& transform   = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
        const auto format = inputFrame->GetPixelFormat();
        result.failure    = "roi_format_unsupported";
        if (format == media::PixelFormat::PIXEL_I420) {
            result.failure = "roi_color_conversion_failed";
            inputFrame     = transform.I4202BGR(inputFrame);
        } else if (format != media::PixelFormat::PIXEL_BGR8 && format != media::PixelFormat::PIXEL_RGB8)
            return result;
        result.failure = "roi_color_conversion_failed";
        if (!VideoFrameValid(inputFrame))
            return result;
        if (strict && (inputFrame->GetWidth() != static_cast<size_t>(result.actual.width) ||
                       inputFrame->GetHeight() != static_cast<size_t>(result.actual.height))) {
            result.failure = "roi_conversion_bounds_mismatch";
            return result;
        }
        inputFrame->SetFrameIndex(frame->GetFrameIndex());
        inputFrame->SetTimestamp(frame->GetTimestamp());
        inputFrame->SetStreamIndex(frame->GetStreamIndex());
        result.failure         = "roi_host_data_unavailable";
        const bool transferred = transform.EnsureHostData(inputFrame);
        // CPU frames already keep host pixels in GetData(); hardware frames
        // populate GetHostData(). A failed transfer must never be accepted by
        // typed review merely because a device-memory handle is non-null.
        const bool hasHost = transferred && inputFrame->GetHostData();
        if ((strict && !transferred) || (!hasHost && !inputFrame->GetData()))
            return result;
        result.failure.clear();
        result.frame = std::move(inputFrame);
        return result;
    }
}  // namespace

AlarmReviewRoi PrepareAlarmReviewRoi(const VideoFramePtr& frame, const util::Box& box) {
    AlarmReviewRoi result;
    return Prepare(frame, box, false, result);
}

AlarmReviewRoi PrepareAlarmReviewRoiStrict(const VideoFramePtr& frame, const util::Box& box) {
    AlarmReviewRoi result;
    try {
        return Prepare(frame, box, true, result);
    } catch (...) {
        // Keep the precise failing preparation stage without exposing backend
        // exception text or replacing the caller's target with the full image.
        if (result.failure.empty())
            result.failure = "roi_preparation_failed";
        result.frame.reset();
        return result;
    }
}
}  // namespace cosmo
