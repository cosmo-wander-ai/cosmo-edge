#pragma once

#include <memory>
#include <shared_mutex>

#include "service/media/IVideoFrameService.h"

namespace cosmo::media {
class IVideoFrameProc;
}

namespace cosmo::service {

class VideoFrameServiceImpl : public IVideoFrameService {
public:
    VideoFrameServiceImpl();
    ~VideoFrameServiceImpl() override;

    VideoFramePtr CopyJpegSrcFrame(VideoFramePtr srcImage) override;

    VideoFramePtr DrawLines(VideoFramePtr srcImage,
                            std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines,
                            const cosmo::media::Color& color, int lineWidth = 2) override;

    VideoFramePtr DrawRects(VideoFramePtr srcImage, const std::vector<cosmo::util::Box>& rects,
                            const cosmo::media::Color& color, int lineWidth = 2) override;

    VideoFramePtr DrawText(VideoFramePtr srcImage, int x, int y, const std::string& text,
                           const cosmo::media::Color& color, int fontSize = 50) override;

    bool BeginOSD(VideoFramePtr frame) override;
    void OSDDrawLines(std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines,
                      const cosmo::media::Color& color, int lineWidth) override;
    void OSDDrawText(int x, int y, const std::string& text, const cosmo::media::Color& color,
                     int fontSize = 50) override;
    void OSDDrawTextEx(int x, int y, const std::string& text, const cosmo::media::Color& color, int fontSize,
                       const cosmo::media::Color& bgColor, uint8_t bgAlpha, bool outline = true,
                       int bgPadding = 4) override;
    void EndOSD() override;

    VideoFramePtr Crop(const VideoFramePtr srcPicture, const cosmo::util::Box roi) override;

    VideoFramePtr Resize(VideoFramePtr src, int dst_height, int dst_width) override;

    VideoFramePtr Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left, size_t right,
                          cosmo::media::Color color) override;

    std::vector<u_char> EncodeJpeg(const VideoFramePtr srcPicture) override;

    VideoFramePtr DecodeJpeg(const std::vector<u_int8_t>& data) override;

    bool EnsureHostData(VideoFramePtr frame) override;

    VideoFramePtr I4202BGR(VideoFramePtr) override;
    VideoFramePtr I4202RGB(VideoFramePtr) override;
    VideoFramePtr BGR2I420(VideoFramePtr) override;
    VideoFramePtr RGB2I420(VideoFramePtr) override;

private:
    std::unique_ptr<cosmo::media::IVideoFrameProc> proc_;
    std::shared_mutex piv_mtx_;  // Protects DrawText and BeginOSD (moved from deleted VideoFrameProc)
};

}  // namespace cosmo::service
