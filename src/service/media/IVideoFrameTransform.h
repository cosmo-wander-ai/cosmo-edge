/// @file IVideoFrameTransform.h
/// @brief Video frame geometric transformation, memory management, and
///        color space conversion interface.
///        ISP split from IVideoFrameService.
///        Consumed by AlgChannelDecode, Qwen3VLWorker, Qwen3VLInference,
///        PTaskBaseUpload, PQwen3VLWorker.
#pragma once

#include <cstddef>
#include <cstdint>

#include "service/media/dto/VideoFrameFwd.h"
#include "util/Rect.h"

namespace cosmo::media {
struct Color;
}

namespace cosmo::service {

/// Geometric transformations, host-memory management, and color space
/// conversion for VideoFrame objects.
class IVideoFrameTransform {
public:
    virtual ~IVideoFrameTransform() = default;

    // ── Image Transformation ──

    /// Crop a region of interest from a frame.
    /// @param srcPicture Source frame.
    /// @param roi        Bounding box to crop.
    /// @return Cropped frame.
    virtual VideoFramePtr Crop(const VideoFramePtr srcPicture, const cosmo::util::Box roi) = 0;

    /// Resize a frame to the specified dimensions.
    /// @param src        Source frame.
    /// @param dst_height Target height in pixels.
    /// @param dst_width  Target width in pixels.
    /// @return Resized frame.
    virtual VideoFramePtr Resize(VideoFramePtr src, int dst_height, int dst_width) = 0;

    /// Add padding to a frame.
    /// @param src    Source frame.
    /// @param top    Top padding in pixels.
    /// @param bottom Bottom padding in pixels.
    /// @param left   Left padding in pixels.
    /// @param right  Right padding in pixels.
    /// @param color  Padding fill color.
    /// @return Padded frame.
    virtual VideoFramePtr Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left, size_t right,
                                  cosmo::media::Color color) = 0;

    // ── Memory Management ──

    /// Ensure the frame's pixel data is accessible from host (CPU) memory.
    /// Triggers a device-to-host DMA transfer if the data resides only in VPU memory.
    /// @param frame Target frame.
    /// @return true on success.
    virtual bool EnsureHostData(VideoFramePtr frame) = 0;

    // ── Color Space Conversion ──

    /// Convert an I420 (YUV) frame to BGR format.
    /// @param frame Source I420 frame.
    /// @return BGR frame.
    virtual VideoFramePtr I4202BGR(VideoFramePtr) = 0;

    /// Convert an I420 (YUV) frame to RGB format.
    /// @param frame Source I420 frame.
    /// @return RGB frame.
    virtual VideoFramePtr I4202RGB(VideoFramePtr) = 0;

    /// Convert a BGR frame to I420 (YUV) format.
    /// @param frame Source BGR frame.
    /// @return I420 frame.
    virtual VideoFramePtr BGR2I420(VideoFramePtr) = 0;

    /// Convert an RGB frame to I420 (YUV) format.
    /// @param frame Source RGB frame.
    /// @return I420 frame.
    virtual VideoFramePtr RGB2I420(VideoFramePtr) = 0;
};

}  // namespace cosmo::service
