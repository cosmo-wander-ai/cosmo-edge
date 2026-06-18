#pragma once

#include "bmcv_api_ext.h"
#include "media/Color.h"
#include "media/IOsdTextRenderer.h"
#include "media/IVideoFrameProc.h"
#include "media/VideoFrame.h"
#include "util/Log.h"
#include "util/Rect.h"

namespace cosmo {
namespace media {
    class VideoFrameProcSophon : public IVideoFrameProc {
    public:
        /// @param mediaHandle  BM1688 device handle (bm_handle_t as void*), obtained from
        ///                     IDeviceContext::GetMediaHandle() by the caller.
        /// @param osdService   OSD text rendering service for TrueType font rendering.
        VideoFrameProcSophon(void* mediaHandle, IOsdTextRenderer& osdService);
        ~VideoFrameProcSophon() override;

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

        // Conversational OSD API — Reuse the same bm_image to reduce Create/Attach/Clean overhead
        bool BeginOSD(VideoFramePtr frame) override;
        void OSDDrawLines(std::vector<std::pair<util::Point, util::Point>> lines, const Color& color,
                          int lineWidth) override;
        void OSDDrawText(int x, int y, const std::string& text, const Color& color,
                         int fontSize = 50) override;
        /// Draw text with semi-transparent background + outline
        /// @param bgColor   Background color
        /// @param bgAlpha   Background opacity 0~255 (0=transparent, 255=opaque)
        /// @param outline   Whether to draw 1px black outline
        void OSDDrawTextEx(int x, int y, const std::string& text, const Color& color, int fontSize,
                           const Color& bgColor, uint8_t bgAlpha, bool outline = true,
                           int bgPadding = 4) override;
        void EndOSD() override;

        /// Initialize TrueType text renderer (call once at startup)
        bool InitTextRenderer(const std::string& fontPath);

        VideoFramePtr Crop(const VideoFramePtr srcPicture, const util::Box roi) override;

        VideoFramePtr Resize(VideoFramePtr src, int dst_height, int dst_width) override;

        VideoFramePtr Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left, size_t right,
                              Color color) override;

        std::vector<u_char> EncodeJpeg(const VideoFramePtr srcPicture) override;

        VideoFramePtr DecodeJpeg(const std::vector<u_int8_t>& data) override;

        VideoFramePtr BGR2I420(VideoFramePtr srcImage) override;

        VideoFramePtr RGB2I420(VideoFramePtr srcImage) override;

        VideoFramePtr I4202BGR(VideoFramePtr srcImage) override;

        VideoFramePtr I4202RGB(VideoFramePtr srcImage) override;

        VideoFramePtr CopyFrame(VideoFramePtr srcImage) override;

        // Copy device memory frame to hostData (internally allocates host buffer automatically)
        // Used for third-party components requiring contiguous host data (e.g., Qwen3VL's
        // process_image_from_mat)
        bool EnsureHostData(VideoFramePtr frame) override;

        struct BmFrameImageDeleter {
            void operator()(bm_image* image) {
                if (image) {
                    if (bm_image_is_attached(*image)) {
                        bm_image_detach(*image);
                    }
                    bm_image_destroy(image);
                    free(image);  // NOLINT: paired with malloc in CreateBMImage, RAII via unique_ptr
                }
            }
        };
        using BmFrameImagePtr = std::unique_ptr<bm_image, BmFrameImageDeleter>;

    private:
        VideoFramePtr DecodeJpegHardware(const std::vector<u_int8_t>& data);

        bool VideoFrameAttach(VideoFramePtr frame, bm_image* image);

        bool CopyFrameFromDevice(VideoFramePtr);

        BmFrameImagePtr VideoFrameToBMImage(VideoFramePtr frame);

        BmFrameImagePtr CreateBMImage(size_t width, size_t height, PixelFormat pf);

        bm_image_format_ext MapImageFormat(PixelFormat pf);

        // Unified pixel format conversion helper — replaces 4 near-identical methods
        VideoFramePtr ConvertPixelFormat(VideoFramePtr frame, PixelFormat src_fmt, PixelFormat dst_fmt,
                                         const char* caller);

    private:
        /// Batch-render queued text entries onto the frame during BeginOSD~EndOSD session
        void FlushTextQueue();

        bm_handle_t handle;
        IOsdTextRenderer& osd_service_;  ///< Injected OSD text renderer
    };

}  // namespace media
}  // namespace cosmo
