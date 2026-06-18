/// @file IVideoFrameProc.h
/// @brief Abstract interface for backend-agnostic video frame processing.
///
/// Both VideoFrameProcSophon (hardware BM1688 VPU) and VideoFrameProcCpu
/// (FFmpeg software) implement this interface.  Consumer code depends only
/// on IVideoFrameProc, and the concrete backend is selected at CMake time
/// via a factory function (see VideoFrameProcFactory.h).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "media/Color.h"
#include "media/VideoFrame.h"
#include "util/Rect.h"

namespace cosmo::media {

class IVideoFrameProc {
public:
    virtual ~IVideoFrameProc() = default;

    // ── Frame copy / host-data ──────────────────────────────────────────
    virtual VideoFramePtr CopyFrame(VideoFramePtr srcImage) = 0;
    virtual bool EnsureHostData(VideoFramePtr frame)        = 0;

    // ── Color conversion ────────────────────────────────────────────────
    virtual VideoFramePtr BGR2I420(VideoFramePtr srcImage) = 0;
    virtual VideoFramePtr RGB2I420(VideoFramePtr srcImage) = 0;
    virtual VideoFramePtr I4202BGR(VideoFramePtr srcImage) = 0;
    virtual VideoFramePtr I4202RGB(VideoFramePtr srcImage) = 0;

    // ── Image processing ────────────────────────────────────────────────
    virtual VideoFramePtr Crop(const VideoFramePtr srcPicture, const util::Box roi) = 0;
    virtual VideoFramePtr Resize(VideoFramePtr src, int dst_height, int dst_width)  = 0;
    virtual VideoFramePtr Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left, size_t right,
                                  Color color)                                      = 0;

    // ── JPEG codec ──────────────────────────────────────────────────────
    virtual std::vector<u_char> EncodeJpeg(const VideoFramePtr srcPicture) = 0;
    virtual VideoFramePtr DecodeJpeg(const std::vector<u_int8_t>& data)    = 0;

    // ── Drawing ─────────────────────────────────────────────────────────
    virtual VideoFramePtr DrawBox(VideoFramePtr srcImage, const util::Box imageRect, const Color& color,
                                  int lineWidth = 2)                       = 0;
    virtual VideoFramePtr DrawPoint(VideoFramePtr srcImage, util::Point point, const Color& color,
                                    int lineWidth = 2)                     = 0;
    virtual VideoFramePtr DrawLines(VideoFramePtr srcImage,
                                    std::vector<std::pair<util::Point, util::Point>> lines,
                                    const Color& color, int lineWidth)     = 0;
    virtual VideoFramePtr DrawRects(VideoFramePtr srcImage, const std::vector<util::Box>& rects,
                                    const Color& color, int lineWidth = 2) = 0;
    virtual VideoFramePtr DrawText(VideoFramePtr srcImage, int x, int y, const std::string& text,
                                   const Color& color, int fontSize = 50)  = 0;

    // ── Session-based OSD ───────────────────────────────────────────────
    virtual bool BeginOSD(VideoFramePtr frame)    = 0;
    virtual void OSDDrawLines(std::vector<std::pair<util::Point, util::Point>> lines, const Color& color,
                              int lineWidth)      = 0;
    virtual void OSDDrawText(int x, int y, const std::string& text, const Color& color,
                             int fontSize = 50)   = 0;
    virtual void OSDDrawTextEx(int x, int y, const std::string& text, const Color& color, int fontSize,
                               const Color& bgColor, uint8_t bgAlpha, bool outline = true,
                               int bgPadding = 4) = 0;
    virtual void EndOSD()                         = 0;
};

}  // namespace cosmo::media
