/// @file IVideoFrameOSD.h
/// @brief Video frame drawing and OSD (On-Screen Display) interface.
///        ISP split from IVideoFrameService.
///        Consumed by StreamViewerOverview, StreamViewerDraw, PTaskBaseUpload.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "service/media/dto/VideoFrameFwd.h"
#include "util/Rect.h"

namespace cosmo::media {
struct Color;
}

namespace cosmo::service {

/// Frame copy and drawing operations — both immediate-mode and batched OSD.
///
/// Immediate-mode methods (DrawLines, DrawRects, DrawText) return a new frame
/// with drawings applied.  Batched OSD (BeginOSD … EndOSD) minimizes VPU
/// round-trips when multiple overlays are needed on a single frame.
class IVideoFrameOSD {
public:
    virtual ~IVideoFrameOSD() = default;

    // ── Copy ──

    /// Create a deep copy of a JPEG source frame.
    /// @param srcImage Source frame.
    /// @return New frame with copied data.
    virtual VideoFramePtr CopyJpegSrcFrame(VideoFramePtr srcImage) = 0;

    // ── Drawing (Immediate Mode) ──

    /// Draw lines on a frame (returns a new frame with drawings applied).
    /// @param srcImage  Source frame.
    /// @param lines     Line segments as (start, end) point pairs.
    /// @param color     Line color.
    /// @param lineWidth Line width in pixels (default: 2).
    /// @return New frame with lines drawn.
    virtual VideoFramePtr DrawLines(VideoFramePtr srcImage,
                                    std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines,
                                    const cosmo::media::Color& color, int lineWidth = 2) = 0;

    /// Draw rectangles on a frame.
    /// @param srcImage  Source frame.
    /// @param rects     Bounding boxes to draw.
    /// @param color     Rectangle color.
    /// @param lineWidth Line width in pixels (default: 2).
    /// @return New frame with rectangles drawn.
    virtual VideoFramePtr DrawRects(VideoFramePtr srcImage, const std::vector<cosmo::util::Box>& rects,
                                    const cosmo::media::Color& color, int lineWidth = 2) = 0;

    /// Draw text on a frame.
    /// @param srcImage Source frame.
    /// @param x        X coordinate of the text origin.
    /// @param y        Y coordinate of the text origin.
    /// @param text     UTF-8 text string.
    /// @param color    Text color.
    /// @param fontSize Font size in pixels (default: 50).
    /// @return New frame with text drawn.
    virtual VideoFramePtr DrawText(VideoFramePtr srcImage, int x, int y, const std::string& text,
                                   const cosmo::media::Color& color, int fontSize = 50) = 0;

    // ── Drawing (Batched OSD API) ──

    /// Begin a batched OSD drawing session on a frame.
    /// @param frame Target frame (must remain valid until EndOSD).
    /// @return true if the session was started successfully.
    virtual bool BeginOSD(VideoFramePtr frame) = 0;

    /// Draw lines in the current OSD session.
    /// @param lines     Line segments as (start, end) point pairs.
    /// @param color     Line color.
    /// @param lineWidth Line width in pixels.
    virtual void OSDDrawLines(std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines,
                              const cosmo::media::Color& color, int lineWidth) = 0;

    /// Draw text in the current OSD session.
    /// @param x        X coordinate.
    /// @param y        Y coordinate.
    /// @param text     UTF-8 text string.
    /// @param color    Text color.
    /// @param fontSize Font size in pixels (default: 50).
    virtual void OSDDrawText(int x, int y, const std::string& text, const cosmo::media::Color& color,
                             int fontSize = 50) = 0;

    /// Draw text with background and optional outline in the current OSD session.
    /// @param x         X coordinate.
    /// @param y         Y coordinate.
    /// @param text      UTF-8 text string.
    /// @param color     Text color.
    /// @param fontSize  Font size in pixels.
    /// @param bgColor   Background rectangle color.
    /// @param bgAlpha   Background opacity (0–255).
    /// @param outline   Whether to draw an outline around the text (default: true).
    /// @param bgPadding Padding around text for the background rect (default: 4).
    virtual void OSDDrawTextEx(int x, int y, const std::string& text, const cosmo::media::Color& color,
                               int fontSize, const cosmo::media::Color& bgColor, uint8_t bgAlpha,
                               bool outline = true, int bgPadding = 4) = 0;

    /// Finalize and commit the batched OSD drawing session.
    virtual void EndOSD() = 0;
};

}  // namespace cosmo::service
