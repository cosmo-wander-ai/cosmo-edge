#pragma once

#include <vector>

#include "media/Color.h"
#include "media/IOsdTextRenderer.h"
#include "media/IVideoFrameProc.h"
#include "media/VideoFrame.h"
#include "util/Log.h"
#include "util/Rect.h"

namespace cosmo {
namespace media {

    /// CPU-backend video frame processing — uses FFmpeg sws_scale + stb_image.
    /// Provides the same interface as VideoFrameProcSophon for image processing,
    /// but operates entirely on host memory (malloc'd Blocks).
    /// Drawing/OSD operations use CPU-native YUV pixel manipulation + stb_truetype.
    class VideoFrameProcCpu : public IVideoFrameProc {
    public:
        /// @param osdService  OSD text rendering service (used for Phase 2 OSD)
        VideoFrameProcCpu(IOsdTextRenderer& osdService);
        ~VideoFrameProcCpu() override;

        VideoFramePtr CopyFrame(VideoFramePtr srcImage) override;

        bool EnsureHostData(VideoFramePtr frame) override;

        // Color conversion
        VideoFramePtr BGR2I420(VideoFramePtr srcImage) override;
        VideoFramePtr RGB2I420(VideoFramePtr srcImage) override;
        VideoFramePtr I4202BGR(VideoFramePtr srcImage) override;
        VideoFramePtr I4202RGB(VideoFramePtr srcImage) override;

        // Image processing
        VideoFramePtr Crop(const VideoFramePtr srcPicture, const util::Box roi) override;
        VideoFramePtr Resize(VideoFramePtr src, int dst_height, int dst_width) override;
        VideoFramePtr Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left, size_t right,
                              Color color) override;

        // JPEG codec
        std::vector<u_char> EncodeJpeg(const VideoFramePtr srcPicture) override;
        VideoFramePtr DecodeJpeg(const std::vector<u_int8_t>& data) override;

        // Drawing operations — CPU-native I420 pixel manipulation
        VideoFramePtr DrawBox(VideoFramePtr srcImage, const util::Box imageRect, const Color& color,
                              int lineWidth = 2) override;
        VideoFramePtr DrawPoint(VideoFramePtr srcImage, util::Point point, const Color& color,
                                int lineWidth = 2) override;
        VideoFramePtr DrawLine(VideoFramePtr srcImage, util::Point point1, util::Point point2,
                               const Color& color, int lineWidth = 2);
        VideoFramePtr DrawLines(VideoFramePtr srcImage,
                                std::vector<std::pair<util::Point, util::Point>> lines, const Color& color,
                                int lineWidth) override;
        VideoFramePtr DrawRects(VideoFramePtr srcImage, const std::vector<util::Box>& rects,
                                const Color& color, int lineWidth = 2) override;
        VideoFramePtr DrawText(VideoFramePtr srcImage, int x, int y, const std::string& text,
                               const Color& color, int fontSize = 50) override;

        // Session-based OSD API — operates on frame set by BeginOSD()
        bool BeginOSD(VideoFramePtr frame) override;
        void OSDDrawLines(std::vector<std::pair<util::Point, util::Point>> lines, const Color& color,
                          int lineWidth) override;
        void OSDDrawText(int x, int y, const std::string& text, const Color& color,
                         int fontSize = 50) override;
        void OSDDrawTextEx(int x, int y, const std::string& text, const Color& color, int fontSize,
                           const Color& bgColor, uint8_t bgAlpha, bool outline = true,
                           int bgPadding = 4) override;
        void EndOSD() override;

        bool InitTextRenderer(const std::string& fontPath);

    private:
        /// Unified pixel format conversion helper using FFmpeg sws_scale
        VideoFramePtr ConvertPixelFormat(VideoFramePtr frame, PixelFormat src_fmt, PixelFormat dst_fmt,
                                         const char* caller);

        /// Map cosmo::media::PixelFormat to FFmpeg AVPixelFormat
        static int MapToAVPixelFormat(PixelFormat pf);

        IOsdTextRenderer& osd_service_;

        /// Current frame for session-based OSD operations (BeginOSD..EndOSD)
        VideoFramePtr osd_frame_{nullptr};
    };

}  // namespace media
}  // namespace cosmo
