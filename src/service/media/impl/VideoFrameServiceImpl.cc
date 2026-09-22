// VideoFrameServiceImpl — Video Frame Service Impl implementation.

#include "service/media/impl/VideoFrameServiceImpl.h"

#include <mutex>

#include "media/IOsdTextRenderer.h"
#include "media/IVideoFrameProc.h"
#include "media/PixelFormat.h"
#include "media/VideoFrameProcFactory.h"
#include "mem/IDeviceContext.h"
#include "service/detail/ServiceRegistry.h"
#include "util/Log.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/log.h>
#ifdef __cplusplus
}
#endif

namespace cosmo::service {

VideoFrameServiceImpl::VideoFrameServiceImpl() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        av_log_set_flags(AV_LOG_SKIP_REPEATED);
        av_log_set_level(AV_LOG_QUIET);
    });

    auto& registry = ServiceRegistry::Instance();
    auto& ctx      = registry.Get<cosmo::mem::IDeviceContext>();
    auto& osd      = registry.Get<cosmo::media::IOsdTextRenderer>();
    proc_          = cosmo::media::CreateVideoFrameProc(ctx, osd);
}

VideoFrameServiceImpl::~VideoFrameServiceImpl() = default;

VideoFramePtr VideoFrameServiceImpl::MosaicCopy(VideoFramePtr src, const std::vector<cosmo::util::Box>& boxes,
                                                int strength) {
    return proc_ ? proc_->MosaicCopy(src, boxes, strength) : nullptr;
}

VideoFramePtr VideoFrameServiceImpl::CopyJpegSrcFrame(VideoFramePtr srcImage) {
    if (!srcImage || !srcImage->Active()) {
        LOG_WARN("INVALID DATA:{}", srcImage ? "IMAGE INVALID " : "IMAGE NULL");
        return srcImage;
    }
    if (proc_) {
#ifdef COSMO_NN_USE_HOST_BACKEND
        // CPU backend: EncodeJpeg uses stb (accepts RGB/BGR/I420 via sws_scale),
        // no need to force I420 like Sophon hardware encoder does.
        // Just copy the frame as-is to avoid lossy BGR→I420→RGB round-trips.
        return proc_->CopyFrame(srcImage);
#else
        if (media::PixelFormat::PIXEL_I420 == srcImage->GetPixelFormat()) {
            return proc_->CopyFrame(srcImage);
        } else if (media::PixelFormat::PIXEL_BGR8 == srcImage->GetPixelFormat()) {
            return proc_->BGR2I420(srcImage);
        } else if (media::PixelFormat::PIXEL_RGB8 == srcImage->GetPixelFormat()) {
            return proc_->RGB2I420(srcImage);
        }
#endif
    }
    return srcImage;
}

VideoFramePtr VideoFrameServiceImpl::DrawLines(
    VideoFramePtr srcImage, std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines,
    const cosmo::media::Color& color, int line_width) {
    if (lines.empty()) {
        return srcImage;
    }
    if (proc_) {
        srcImage = proc_->DrawLines(srcImage, lines, color, line_width);
    }
    return srcImage;
}

VideoFramePtr VideoFrameServiceImpl::DrawRects(VideoFramePtr srcImage,
                                               const std::vector<cosmo::util::Box>& rects,
                                               const cosmo::media::Color& color, int line_width) {
    if (rects.empty()) {
        return srcImage;
    }
    if (proc_) {
        srcImage = proc_->DrawRects(srcImage, rects, color, line_width);
    }
    return srcImage;
}

VideoFramePtr VideoFrameServiceImpl::DrawText(VideoFramePtr srcImage, int x, int y, const std::string& text,
                                              const cosmo::media::Color& color, int font_size) {
    if (proc_) {
        std::lock_guard<std::shared_mutex> lock(piv_mtx_);
        return proc_->DrawText(srcImage, x, y, text, color, font_size);
    }
    return srcImage;
}

bool VideoFrameServiceImpl::BeginOSD(VideoFramePtr frame) {
    osd_session_mtx_.lock();
    if (osd_session_active_) {
        // Only the owner can acquire the recursive mutex while a session is
        // active. Reject a nested session instead of replacing its frame.
        LOG_ERRO("{}", "BeginOSD rejected: current thread already owns an OSD session");
        osd_session_mtx_.unlock();
        return false;
    }

    try {
        if (!proc_ || !proc_->BeginOSD(frame)) {
            osd_session_mtx_.unlock();
            return false;
        }
    } catch (...) {
        osd_session_mtx_.unlock();
        throw;
    }

    osd_session_active_ = true;
    osd_session_owner_  = std::this_thread::get_id();
    return true;
}

void VideoFrameServiceImpl::OSDDrawLines(std::vector<std::pair<cosmo::util::Point, cosmo::util::Point>> lines,
                                         const cosmo::media::Color& color, int line_width) {
    std::lock_guard<std::recursive_mutex> lock(osd_session_mtx_);
    if (proc_ && osd_session_active_ && osd_session_owner_ == std::this_thread::get_id()) {
        proc_->OSDDrawLines(lines, color, line_width);
    }
}

void VideoFrameServiceImpl::OSDDrawText(int x, int y, const std::string& text,
                                        const cosmo::media::Color& color, int font_size) {
    std::lock_guard<std::recursive_mutex> lock(osd_session_mtx_);
    if (proc_ && osd_session_active_ && osd_session_owner_ == std::this_thread::get_id()) {
        proc_->OSDDrawText(x, y, text, color, font_size);
    }
}

void VideoFrameServiceImpl::OSDDrawTextEx(int x, int y, const std::string& text,
                                          const cosmo::media::Color& color, int font_size,
                                          const cosmo::media::Color& bgColor, uint8_t bgAlpha, bool outline,
                                          int bg_padding) {
    std::lock_guard<std::recursive_mutex> lock(osd_session_mtx_);
    if (proc_ && osd_session_active_ && osd_session_owner_ == std::this_thread::get_id()) {
        proc_->OSDDrawTextEx(x, y, text, color, font_size, bgColor, bgAlpha, outline, bg_padding);
    }
}

void VideoFrameServiceImpl::EndOSD() {
    std::lock_guard<std::recursive_mutex> lock(osd_session_mtx_);
    if (!osd_session_active_ || osd_session_owner_ != std::this_thread::get_id()) {
        return;
    }

    try {
        if (proc_) {
            proc_->EndOSD();
        }
    } catch (...) {
        osd_session_active_ = false;
        osd_session_owner_  = {};
        osd_session_mtx_.unlock();
        throw;
    }

    osd_session_active_ = false;
    osd_session_owner_  = {};
    // Release the lock retained by BeginOSD. The lock_guard releases the
    // recursive acquisition made at the beginning of EndOSD.
    osd_session_mtx_.unlock();
}

VideoFramePtr VideoFrameServiceImpl::Crop(const VideoFramePtr src_picture, const cosmo::util::Box roi) {
    return proc_->Crop(src_picture, roi);
}

VideoFramePtr VideoFrameServiceImpl::Resize(VideoFramePtr src, int dst_height, int dst_width) {
    if (proc_) {
        return proc_->Resize(src, dst_height, dst_width);
    }
    return {};
}

VideoFramePtr VideoFrameServiceImpl::Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left,
                                             size_t right, cosmo::media::Color color) {
    if (proc_) {
        return proc_->Padding(src, top, bottom, left, right, color);
    }
    return {};
}

std::vector<u_char> VideoFrameServiceImpl::EncodeJpeg(const VideoFramePtr src_picture) {
    if (proc_) {
        return proc_->EncodeJpeg(src_picture);
    }
    return {};
}

VideoFramePtr VideoFrameServiceImpl::DecodeJpeg(const std::vector<u_int8_t>& data) {
    if (proc_) {
        return proc_->DecodeJpeg(data);
    }
    return nullptr;
}

bool VideoFrameServiceImpl::EnsureHostData(VideoFramePtr frame) {
    if (!frame || !frame->Active()) {
        return false;
    }
    if (frame->GetHostData()) {
        return true;
    }
    if (proc_) {
        return proc_->EnsureHostData(frame);
    }
    return false;
}

VideoFramePtr VideoFrameServiceImpl::I4202BGR(VideoFramePtr frame) {
    if (proc_) {
        return proc_->I4202BGR(frame);
    }
    return nullptr;
}
VideoFramePtr VideoFrameServiceImpl::I4202RGB(VideoFramePtr frame) {
    if (proc_) {
        return proc_->I4202RGB(frame);
    }
    return nullptr;
}

VideoFramePtr VideoFrameServiceImpl::BGR2I420(VideoFramePtr frame) {
    if (proc_) {
        return proc_->BGR2I420(frame);
    }
    return nullptr;
}

VideoFramePtr VideoFrameServiceImpl::RGB2I420(VideoFramePtr frame) {
    if (proc_) {
        return proc_->RGB2I420(frame);
    }
    return nullptr;
}

}  // namespace cosmo::service
