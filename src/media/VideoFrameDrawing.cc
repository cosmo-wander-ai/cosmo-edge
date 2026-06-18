// VideoFrameDrawing.cc — Standalone drawing operations for VideoFrameProcSophon.
// Split from VideoFrameProcSophon.cc to reduce file size (DEBT-007).
// Session-based OSD API lives in VideoFrameOsd.cc (Phase 2 file split).

#include <vector>

#include "bmcv_api_ext.h"
#include "media/PixelFormatUtils.h"
#include "media/VideoFrameProcSophon.h"
#include "util/Log.h"

namespace cosmo {
namespace media {

    VideoFramePtr VideoFrameProcSophon::DrawBox(VideoFramePtr srcImage, const util::Box imageRect,
                                                const Color& color, int lineWidth) {
        if (!VideoFrameValid(srcImage, true)) {
            return srcImage;
        }

        VideoFramePtr frame = srcImage;
        if (frame->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "DrawBox() - FrameType Not Support");
            return srcImage;
        }

        auto height = frame->GetHeight();
        auto width  = frame->GetWidth();

        auto image = CreateBMImage(width, height, frame->GetPixelFormat());
        if (!image) {
            LOG_WARN("{}", "DrawBox() - CreateBMImage failed");
            return frame;
        }

        if (!VideoFrameAttach(frame, image.get())) {
            LOG_WARN("{}", "DrawBox() - VideoFrameAttach failed");
            return frame;
        }

        bmcv_rect_t rect;
        rect.start_x = static_cast<unsigned int>(imageRect.x);
        rect.start_y = static_cast<unsigned int>(imageRect.y);
        rect.crop_w  = static_cast<unsigned int>(imageRect.width);
        rect.crop_h  = static_cast<unsigned int>(imageRect.height);

        auto ret = bmcv_image_draw_rectangle(handle, *image, 1, &rect, lineWidth, color.red, color.green,
                                             color.blue);

        if (ret != BM_SUCCESS) {
            LOG_WARN("{}", "bmcv_image_draw_rectangle failed");
            return frame;
        }

        return frame;
    }

    VideoFramePtr VideoFrameProcSophon::DrawPoint(VideoFramePtr /*srcImage*/, util::Point /*point*/,
                                                  const Color& /*color*/, int /*lineWidth*/) {
        return nullptr;
    }

    VideoFramePtr VideoFrameProcSophon::DrawLine(VideoFramePtr src, util::Point point1, util::Point point2,
                                                 const Color& color, int line_width) {
        std::pair<util::Point, util::Point> line = {point1, point2};
        return DrawLines(src, {line}, color, line_width);
    }

    VideoFramePtr VideoFrameProcSophon::DrawLines(VideoFramePtr srcImage,
                                                  std::vector<std::pair<util::Point, util::Point>> lines,
                                                  const Color& color, int lineWidth) {
        if (!VideoFrameValid(srcImage, true)) {
            return srcImage;
        }

        VideoFramePtr frame = srcImage;
        if (frame->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("FrameType {} Not Support ", frame->GetPixelFormat());
            return srcImage;
        }

        constexpr size_t lineSize = 128;
        bmcv_point_t start[lineSize];
        bmcv_point_t end[lineSize];

        size_t lineCount = 0;
        for (auto& line : lines) {
            if (lineCount >= lineSize) {
                break;
            }
            if ((line.first.x < 0) || (line.first.y < 0) || (line.second.x < 0) || (line.second.y < 0) ||
                (line.first.x > static_cast<int>(srcImage->GetWidth())) ||
                (line.first.y > static_cast<int>(srcImage->GetHeight())) ||
                (line.second.x > static_cast<int>(srcImage->GetWidth())) ||
                (line.second.y > static_cast<int>(srcImage->GetHeight()))) {
                LOG_WARN("Point:({}.{}) or ({}.{}) Not On Image:{}x{}", line.first.x, line.first.y,
                         line.second.x, line.second.y, srcImage->GetWidth(), srcImage->GetHeight());
                continue;
            }

            start[lineCount].x = line.first.x;
            start[lineCount].y = line.first.y;
            end[lineCount].x   = line.second.x;
            end[lineCount].y   = line.second.y;
            lineCount += 1;
        }

        if ((lineCount == 0) || (lineCount > lineSize)) {
            LOG_ERRO("lineCount {} Not Support ", lineCount);
            return srcImage;
        }
        int lineNum = static_cast<int>(lineCount);

        bmcv_color_t c;
        c.r = color.red;
        c.g = color.green;
        c.b = color.blue;

        auto height = frame->GetHeight();
        auto width  = frame->GetWidth();

        auto image = CreateBMImage(width, height, frame->GetPixelFormat());
        if (!image) {
            LOG_WARN("{}", "DrawLines() - CreateBMImage failed");
            return frame;
        }

        if (!VideoFrameAttach(frame, image.get())) {
            LOG_WARN("{}", "DrawLines() - VideoFrameAttach failed ");
            return frame;
        }

        auto ret = bmcv_image_draw_lines(handle, *image, start, end, lineNum, c, lineWidth);
        if (ret != BM_SUCCESS) {
            LOG_WARN("bmcv_image_draw_lines failed {}", ret);
        }

        return frame;
    }

    VideoFramePtr VideoFrameProcSophon::DrawRects(VideoFramePtr srcImage, const std::vector<util::Box>& rects,
                                                  const Color& color, int lineWidth) {
        if (!VideoFrameValid(srcImage, true)) {
            return srcImage;
        }

        VideoFramePtr frame = srcImage;
        if (frame->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "DrawRects() - FrameType Not Support");
            return srcImage;
        }

        auto height = frame->GetHeight();
        auto width  = frame->GetWidth();

        auto image = CreateBMImage(width, height, frame->GetPixelFormat());
        if (!image) {
            LOG_WARN("{}", "DrawRects() - CreateBMImage failed");
            return frame;
        }

        if (!VideoFrameAttach(frame, image.get())) {
            LOG_WARN("{}", "DrawRects() - VideoFrameAttach failed");
            return frame;
        }

        const size_t maxRects = 128;
        bmcv_rect_t bmRects[maxRects];
        size_t count = 0;
        for (auto& r : rects) {
            if (count >= maxRects)
                break;
            if (r.x < 0 || r.y < 0 || r.x + r.width > static_cast<int>(width) ||
                r.y + r.height > static_cast<int>(height)) {
                continue;
            }
            bmRects[count].start_x = static_cast<unsigned int>(r.x);
            bmRects[count].start_y = static_cast<unsigned int>(r.y);
            bmRects[count].crop_w  = static_cast<unsigned int>(r.width);
            bmRects[count].crop_h  = static_cast<unsigned int>(r.height);
            count++;
        }

        if (count > 0) {
            auto ret = bmcv_image_draw_rectangle(handle, *image, static_cast<int>(count), bmRects, lineWidth,
                                                 color.red, color.green, color.blue);
            if (ret != BM_SUCCESS) {
                LOG_WARN("bmcv_image_draw_rectangle failed {}", ret);
            }
        }
        return frame;
    }

    VideoFramePtr VideoFrameProcSophon::DrawText(VideoFramePtr srcImage, int x, int y,
                                                 const std::string& text, const Color& color, int fontSize) {
        if (!VideoFrameValid(srcImage, true)) {
            return nullptr;
        }

        VideoFramePtr frame = srcImage;
        if (frame->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "FrameType Not Support for DrawText");
            return srcImage;
        }

        bmcv_point_t point;
        point.x = x;
        point.y = y;

        bmcv_color_t c;
        c.r = color.red;
        c.g = color.green;
        c.b = color.blue;

        auto height = frame->GetHeight();
        auto width  = frame->GetWidth();

        auto image = CreateBMImage(width, height, frame->GetPixelFormat());
        if (!image) {
            LOG_WARN("{}", "DrawText() - CreateBMImage failed");
            return frame;
        }

        if (!VideoFrameAttach(frame, image.get())) {
            LOG_WARN("{}", "DrawText() - VideoFrameAttach failed");
            return frame;
        }

        float fontScale = static_cast<float>(fontSize) / 10.0f;
        auto ret        = bmcv_image_put_text(handle, *image, text.c_str(), point, c, fontScale, 0);
        if (ret != BM_SUCCESS) {
            LOG_WARN("bmcv_image_put_text failed {}", ret);
        }

        return frame;
    }

}  // namespace media
}  // namespace cosmo
