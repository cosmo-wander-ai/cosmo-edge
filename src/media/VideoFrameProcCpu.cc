// VideoFrameProcCpu.cc — CPU-backend image processing using FFmpeg sws_scale + stb_image.
// Drawing/OSD operations implemented with CPU-native YUV pixel manipulation + stb_truetype.

#include "media/VideoFrameProcCpu.h"

#include <algorithm>
#include <cstring>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif
#include "libswscale/swscale.h"
#ifdef __cplusplus
}
#endif

#include "media/PixelFormatUtils.h"
#include "util/Log.h"

// stb implementations — these macros are normally defined in VideoFrameCodec.cc
// (Sophon-only), so the CPU build needs them here.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace cosmo {
namespace media {
    namespace {
        inline uint8_t ClampToByte(int value) {
            return static_cast<uint8_t>(std::clamp(value, 0, 255));
        }

        VideoFramePtr ConvertI420ToPacked(VideoFramePtr frame, PixelFormat dst_fmt, const char* caller) {
            if (!VideoFrameValid(frame)) {
                LOG_ERRO("{}() - invalid frame", caller);
                return nullptr;
            }
            if (frame->GetPixelFormat() != PixelFormat::PIXEL_I420) {
                LOG_ERRO("{}() - source pixel format {} is not I420", caller,
                         static_cast<int>(frame->GetPixelFormat()));
                return nullptr;
            }
            if (dst_fmt != PixelFormat::PIXEL_BGR8 && dst_fmt != PixelFormat::PIXEL_RGB8) {
                LOG_ERRO("{}() - unsupported destination pixel format {}", caller, static_cast<int>(dst_fmt));
                return nullptr;
            }

            auto w = static_cast<int>(frame->GetWidth());
            auto h = static_cast<int>(frame->GetHeight());
            auto dst =
                std::make_shared<VideoFrame>(w, h, dst_fmt, frame->GetFrameIndex(), frame->GetTimestamp());
            if (!VideoFrameValid(dst)) {
                return nullptr;
            }

            auto* yuv = frame->GetData();
            auto* out = dst->GetData();
            if (!yuv || !out) {
                return nullptr;
            }

            const uint8_t* y_plane = yuv;
            const uint8_t* u_plane = yuv + w * h;
            const uint8_t* v_plane = u_plane + (w / 2) * (h / 2);
            const bool dst_bgr     = (dst_fmt == PixelFormat::PIXEL_BGR8);

            for (int py = 0; py < h; py++) {
                for (int px = 0; px < w; px++) {
                    int yv = y_plane[py * w + px];
                    int uv = u_plane[(py / 2) * (w / 2) + (px / 2)];
                    int vv = v_plane[(py / 2) * (w / 2) + (px / 2)];

                    int c = yv - 16;
                    int d = uv - 128;
                    int e = vv - 128;

                    uint8_t r = ClampToByte((298 * c + 409 * e + 128) >> 8);
                    uint8_t g = ClampToByte((298 * c - 100 * d - 208 * e + 128) >> 8);
                    uint8_t b = ClampToByte((298 * c + 516 * d + 128) >> 8);

                    int idx = (py * w + px) * 3;
                    if (dst_bgr) {
                        out[idx + 0] = b;
                        out[idx + 1] = g;
                        out[idx + 2] = r;
                    } else {
                        out[idx + 0] = r;
                        out[idx + 1] = g;
                        out[idx + 2] = b;
                    }
                }
            }

            return dst;
        }

        VideoFramePtr ConvertBgrToRgb(VideoFramePtr frame, const char* caller) {
            if (!VideoFrameValid(frame)) {
                LOG_ERRO("{}() - invalid frame", caller);
                return nullptr;
            }
            if (frame->GetPixelFormat() != PixelFormat::PIXEL_BGR8) {
                LOG_ERRO("{}() - source pixel format {} is not BGR8", caller,
                         static_cast<int>(frame->GetPixelFormat()));
                return nullptr;
            }

            auto w   = static_cast<int>(frame->GetWidth());
            auto h   = static_cast<int>(frame->GetHeight());
            auto dst = std::make_shared<VideoFrame>(w, h, PixelFormat::PIXEL_RGB8, frame->GetFrameIndex(),
                                                    frame->GetTimestamp());
            if (!VideoFrameValid(dst)) {
                return nullptr;
            }

            auto* src = frame->GetData();
            auto* out = dst->GetData();
            if (!src || !out) {
                return nullptr;
            }

            const int pixel_count = w * h;
            for (int i = 0; i < pixel_count; i++) {
                int idx      = i * 3;
                out[idx + 0] = src[idx + 2];
                out[idx + 1] = src[idx + 1];
                out[idx + 2] = src[idx + 0];
            }
            return dst;
        }
    }  // namespace

    VideoFrameProcCpu::VideoFrameProcCpu(IOsdTextRenderer& osdService) : osd_service_(osdService) {}

    VideoFrameProcCpu::~VideoFrameProcCpu() {}

    VideoFramePtr VideoFrameProcCpu::CopyFrame(VideoFramePtr frame) {
        if (!VideoFrameValid(frame, true)) {
            return nullptr;
        }

        auto dst = std::make_shared<VideoFrame>(static_cast<int>(frame->GetWidth()),
                                                static_cast<int>(frame->GetHeight()), frame->GetPixelFormat(),
                                                frame->GetFrameIndex(), frame->GetTimestamp());
        if (!VideoFrameValid(dst, true)) {
            return nullptr;
        }
        dst->SetStreamIndex(frame->GetStreamIndex());

        auto* src_data = frame->GetData();
        auto* dst_data = dst->GetData();
        if (src_data && dst_data) {
            std::memcpy(dst_data, src_data, frame->GetSize());
        }
        return dst;
    }

    bool VideoFrameProcCpu::EnsureHostData(VideoFramePtr frame) {
        // CPU backend: GetData() already points to host memory, no device-to-host copy needed
        if (!frame || !frame->Active()) {
            return false;
        }
        return frame->GetData() != nullptr;
    }

    int VideoFrameProcCpu::MapToAVPixelFormat(PixelFormat pf) {
        switch (pf) {
            case PixelFormat::PIXEL_RGB8:
                return AV_PIX_FMT_RGB24;
            case PixelFormat::PIXEL_BGR8:
                return AV_PIX_FMT_BGR24;
            case PixelFormat::PIXEL_RGBA8:
                return AV_PIX_FMT_RGBA;
            case PixelFormat::PIXEL_BGRA8:
                return AV_PIX_FMT_BGRA;
            case PixelFormat::PIXEL_I420:
                return AV_PIX_FMT_YUV420P;
            case PixelFormat::PIXEL_NV12:
                return AV_PIX_FMT_NV12;
            case PixelFormat::PIXEL_NV21:
                return AV_PIX_FMT_NV21;
            case PixelFormat::PIXEL_YUYV:
                return AV_PIX_FMT_YUYV422;
            case PixelFormat::PIXEL_UYVY:
                return AV_PIX_FMT_UYVY422;
            default:
                return AV_PIX_FMT_NONE;
        }
    }

    VideoFramePtr VideoFrameProcCpu::ConvertPixelFormat(VideoFramePtr frame, PixelFormat src_fmt,
                                                        PixelFormat dst_fmt, const char* caller) {
        if (!VideoFrameValid(frame)) {
            LOG_ERRO("{}() - invalid frame", caller);
            return nullptr;
        }

        auto w = static_cast<int>(frame->GetWidth());
        auto h = static_cast<int>(frame->GetHeight());

        int av_src_fmt = MapToAVPixelFormat(src_fmt);
        int av_dst_fmt = MapToAVPixelFormat(dst_fmt);
        if (av_src_fmt == AV_PIX_FMT_NONE || av_dst_fmt == AV_PIX_FMT_NONE) {
            LOG_ERRO("{}() - unsupported pixel format conversion", caller);
            return nullptr;
        }

        auto* sws_ctx =
            sws_getContext(w, h, static_cast<AVPixelFormat>(av_src_fmt), w, h,
                           static_cast<AVPixelFormat>(av_dst_fmt), SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_ctx) {
            LOG_ERRO("{}() - sws_getContext failed", caller);
            return nullptr;
        }

        auto dst = std::make_shared<VideoFrame>(w, h, dst_fmt);
        if (!VideoFrameValid(dst)) {
            sws_freeContext(sws_ctx);
            return nullptr;
        }

        auto* src_data = frame->GetData();
        auto* dst_data = dst->GetData();

        // Set up source planes based on format
        uint8_t* src_planes[4] = {nullptr};
        int src_strides[4]     = {0};
        uint8_t* dst_planes[4] = {nullptr};
        int dst_strides[4]     = {0};

        auto setup_planes = [&](uint8_t* data, PixelFormat pf, uint8_t** planes, int* strides) {
            switch (pf) {
                case PixelFormat::PIXEL_I420:
                    planes[0]  = data;
                    planes[1]  = data + w * h;
                    planes[2]  = planes[1] + w * h / 4;
                    strides[0] = w;
                    strides[1] = w / 2;
                    strides[2] = w / 2;
                    break;
                case PixelFormat::PIXEL_NV12:
                case PixelFormat::PIXEL_NV21:
                    planes[0]  = data;
                    planes[1]  = data + w * h;
                    strides[0] = w;
                    strides[1] = w;
                    break;
                case PixelFormat::PIXEL_RGB8:
                case PixelFormat::PIXEL_BGR8:
                    planes[0]  = data;
                    strides[0] = w * 3;
                    break;
                case PixelFormat::PIXEL_RGBA8:
                case PixelFormat::PIXEL_BGRA8:
                    planes[0]  = data;
                    strides[0] = w * 4;
                    break;
                default:
                    planes[0]  = data;
                    strides[0] = w * 3;
                    break;
            }
        };

        setup_planes(src_data, src_fmt, src_planes, src_strides);
        setup_planes(dst_data, dst_fmt, dst_planes, dst_strides);

        sws_scale(sws_ctx, src_planes, src_strides, 0, h, dst_planes, dst_strides);
        sws_freeContext(sws_ctx);

        return dst;
    }

    VideoFramePtr VideoFrameProcCpu::BGR2I420(VideoFramePtr frame) {
        return ConvertPixelFormat(frame, PixelFormat::PIXEL_BGR8, PixelFormat::PIXEL_I420, "BGR2I420");
    }

    VideoFramePtr VideoFrameProcCpu::RGB2I420(VideoFramePtr frame) {
        return ConvertPixelFormat(frame, PixelFormat::PIXEL_RGB8, PixelFormat::PIXEL_I420, "RGB2I420");
    }

    VideoFramePtr VideoFrameProcCpu::I4202BGR(VideoFramePtr frame) {
        return ConvertI420ToPacked(frame, PixelFormat::PIXEL_BGR8, "I4202BGR");
    }

    VideoFramePtr VideoFrameProcCpu::I4202RGB(VideoFramePtr frame) {
        return ConvertI420ToPacked(frame, PixelFormat::PIXEL_RGB8, "I4202RGB");
    }

    VideoFramePtr VideoFrameProcCpu::Crop(const VideoFramePtr srcPicture, const util::Box roi) {
        if (!VideoFrameValid(srcPicture)) {
            return nullptr;
        }

        auto pf = srcPicture->GetPixelFormat();
        auto w  = static_cast<int>(srcPicture->GetWidth());
        auto h  = static_cast<int>(srcPicture->GetHeight());

        // Validate ROI
        if (roi.x < 0 || roi.y < 0 || roi.x + roi.width > w || roi.y + roi.height > h) {
            LOG_ERRO("{}", "Crop() - ROI out of bounds");
            return nullptr;
        }

        if (pf == PixelFormat::PIXEL_BGR8 || pf == PixelFormat::PIXEL_RGB8) {
            // Packed 3-channel crop
            auto result = std::make_shared<VideoFrame>(roi.width, roi.height, pf, srcPicture->GetFrameIndex(),
                                                       srcPicture->GetTimestamp());
            if (!VideoFrameValid(result))
                return nullptr;
            auto* src_data = srcPicture->GetData();
            auto* dst_data = result->GetData();
            int row_bytes  = roi.width * 3;
            for (int row = 0; row < roi.height; ++row) {
                std::memcpy(dst_data + row * row_bytes, src_data + ((roi.y + row) * w + roi.x) * 3,
                            row_bytes);
            }
            return result;
        }

        if (pf != PixelFormat::PIXEL_I420) {
            LOG_ERRO("Crop() - PixelFormat {} not supported", static_cast<int>(pf));
            return nullptr;
        }

        auto result = std::make_shared<VideoFrame>(roi.width, roi.height, PixelFormat::PIXEL_I420,
                                                   srcPicture->GetFrameIndex(), srcPicture->GetTimestamp());
        if (!VideoFrameValid(result)) {
            return nullptr;
        }

        auto* src_data = srcPicture->GetData();
        auto* dst_data = result->GetData();

        // Crop Y plane
        for (int row = 0; row < roi.height; ++row) {
            std::memcpy(dst_data + row * roi.width, src_data + (roi.y + row) * w + roi.x, roi.width);
        }

        // Crop U plane
        auto* src_u = src_data + w * h;
        auto* dst_u = dst_data + roi.width * roi.height;
        int half_w  = roi.width / 2;
        int half_h  = roi.height / 2;
        int src_hw  = w / 2;
        for (int row = 0; row < half_h; ++row) {
            std::memcpy(dst_u + row * half_w, src_u + (roi.y / 2 + row) * src_hw + roi.x / 2, half_w);
        }

        // Crop V plane
        auto* src_v = src_u + w * h / 4;
        auto* dst_v = dst_u + half_w * half_h;
        for (int row = 0; row < half_h; ++row) {
            std::memcpy(dst_v + row * half_w, src_v + (roi.y / 2 + row) * src_hw + roi.x / 2, half_w);
        }

        return result;
    }

    VideoFramePtr VideoFrameProcCpu::Resize(VideoFramePtr src, int dst_height, int dst_width) {
        if (dst_height <= 0 || dst_width <= 0) {
            LOG_ERRO("Resize() - invalid dimensions {}x{}", dst_width, dst_height);
            return nullptr;
        }
        if (!VideoFrameValid(src, true)) {
            return nullptr;
        }
        if (src->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "Resize() - only I420 supported");
            return nullptr;
        }

        auto src_w = static_cast<int>(src->GetWidth());
        auto src_h = static_cast<int>(src->GetHeight());

        auto* sws_ctx = sws_getContext(src_w, src_h, AV_PIX_FMT_YUV420P, dst_width, dst_height,
                                       AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_ctx) {
            LOG_ERRO("{}", "Resize() - sws_getContext failed");
            return nullptr;
        }

        auto result = std::make_shared<VideoFrame>(dst_width, dst_height, src->GetPixelFormat(),
                                                   src->GetFrameIndex(), src->GetTimestamp());
        if (!VideoFrameValid(result)) {
            sws_freeContext(sws_ctx);
            return nullptr;
        }

        auto* src_data = src->GetData();
        auto* dst_data = result->GetData();

        uint8_t* src_planes[3] = {src_data, src_data + src_w * src_h, src_data + src_w * src_h * 5 / 4};
        int src_strides[3]     = {src_w, src_w / 2, src_w / 2};

        uint8_t* dst_planes[3] = {dst_data, dst_data + dst_width * dst_height,
                                  dst_data + dst_width * dst_height * 5 / 4};
        int dst_strides[3]     = {dst_width, dst_width / 2, dst_width / 2};

        sws_scale(sws_ctx, src_planes, src_strides, 0, src_h, dst_planes, dst_strides);
        sws_freeContext(sws_ctx);

        return result;
    }

    VideoFramePtr VideoFrameProcCpu::Padding(VideoFramePtr src, size_t top, size_t bottom, size_t left,
                                             size_t right, Color color) {
        if (!VideoFrameValid(src, true)) {
            return nullptr;
        }
        if (src->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "Padding() - only I420 supported");
            return nullptr;
        }

        auto w = static_cast<int>(src->GetWidth());
        auto h = static_cast<int>(src->GetHeight());

        auto new_w = static_cast<int>(w + left + right);
        auto new_h = static_cast<int>(h + top + bottom);

        auto result = std::make_shared<VideoFrame>(new_w, new_h, src->GetPixelFormat());
        if (!VideoFrameValid(result)) {
            return nullptr;
        }

        auto* dst_data = result->GetData();
        auto* src_data = src->GetData();

        // Convert padding color to YUV
        float rf = color.red, gf = color.green, bf = color.blue;
        auto pad_y = static_cast<uint8_t>(std::clamp(0.299f * rf + 0.587f * gf + 0.114f * bf, 0.f, 255.f));
        auto pad_u =
            static_cast<uint8_t>(std::clamp(128.f - 0.169f * rf - 0.331f * gf + 0.500f * bf, 0.f, 255.f));
        auto pad_v =
            static_cast<uint8_t>(std::clamp(128.f + 0.500f * rf - 0.419f * gf - 0.081f * bf, 0.f, 255.f));

        // Fill Y plane
        std::memset(dst_data, pad_y, new_w * new_h);
        for (int row = 0; row < h; ++row) {
            std::memcpy(dst_data + (top + row) * new_w + left, src_data + row * w, w);
        }

        // Fill U plane
        auto* dst_u = dst_data + new_w * new_h;
        auto* src_u = src_data + w * h;
        int new_hw  = new_w / 2;
        int new_hh  = new_h / 2;
        int hw      = w / 2;
        int hh      = h / 2;
        std::memset(dst_u, pad_u, new_hw * new_hh);
        for (int row = 0; row < hh; ++row) {
            std::memcpy(dst_u + (top / 2 + row) * new_hw + left / 2, src_u + row * hw, hw);
        }

        // Fill V plane
        auto* dst_v = dst_u + new_hw * new_hh;
        auto* src_v = src_u + hw * hh;
        std::memset(dst_v, pad_v, new_hw * new_hh);
        for (int row = 0; row < hh; ++row) {
            std::memcpy(dst_v + (top / 2 + row) * new_hw + left / 2, src_v + row * hw, hw);
        }

        return result;
    }

    // stb_image_write callback
    static void CpuCodecMemoryWriteFunc(void* ctx, void* data, int size) {
        auto* buffer = reinterpret_cast<std::vector<uint8_t>*>(ctx);
        auto* src    = reinterpret_cast<const uint8_t*>(data);
        buffer->insert(buffer->end(), src, src + size);
    }

    static constexpr int kCpuImageDefaultSaveQuality = 95;

    std::vector<u_char> VideoFrameProcCpu::EncodeJpeg(const VideoFramePtr src) {
        if (!VideoFrameValid(src)) {
            return {};
        }

        auto w = static_cast<int>(src->GetWidth());
        auto h = static_cast<int>(src->GetHeight());

        // Convert to RGB if needed
        VideoFramePtr rgb_frame = nullptr;
        if (src->GetPixelFormat() == PixelFormat::PIXEL_I420) {
            // Hand-coded I420→RGB8 conversion to avoid potential sws_scale issues
            // on x86 with --disable-x86asm builds
            rgb_frame = std::make_shared<VideoFrame>(w, h, PixelFormat::PIXEL_RGB8);
            if (!VideoFrameValid(rgb_frame)) {
                return {};
            }
            auto* yuv             = src->GetData();
            auto* rgb             = rgb_frame->GetData();
            const uint8_t* yPlane = yuv;
            const uint8_t* uPlane = yuv + w * h;
            const uint8_t* vPlane = uPlane + (w / 2) * (h / 2);
            for (int py = 0; py < h; py++) {
                for (int px = 0; px < w; px++) {
                    int Y        = yPlane[py * w + px];
                    int U        = uPlane[(py / 2) * (w / 2) + (px / 2)];
                    int V        = vPlane[(py / 2) * (w / 2) + (px / 2)];
                    int C        = Y - 16;
                    int D        = U - 128;
                    int E        = V - 128;
                    int R        = std::clamp((298 * C + 409 * E + 128) >> 8, 0, 255);
                    int G        = std::clamp((298 * C - 100 * D - 208 * E + 128) >> 8, 0, 255);
                    int B        = std::clamp((298 * C + 516 * D + 128) >> 8, 0, 255);
                    int idx      = (py * w + px) * 3;
                    rgb[idx]     = static_cast<uint8_t>(R);
                    rgb[idx + 1] = static_cast<uint8_t>(G);
                    rgb[idx + 2] = static_cast<uint8_t>(B);
                }
            }
        } else if (src->GetPixelFormat() == PixelFormat::PIXEL_BGR8) {
            rgb_frame = ConvertBgrToRgb(src, "EncodeJpeg");
            if (!rgb_frame) {
                return {};
            }
        } else if (src->GetPixelFormat() != PixelFormat::PIXEL_RGB8) {
            rgb_frame = ConvertPixelFormat(src, src->GetPixelFormat(), PixelFormat::PIXEL_RGB8, "EncodeJpeg");
            if (!rgb_frame) {
                return {};
            }
        } else {
            rgb_frame = src;
        }

        auto channel = PixelFormatUtils::PixelFormatChannels(PixelFormat::PIXEL_RGB8);
        std::vector<uint8_t> buffer;
        buffer.reserve(w * h * channel / 10);

        auto success = stbi_write_jpg_to_func(
            CpuCodecMemoryWriteFunc, &buffer, w, h, static_cast<int>(channel),
            reinterpret_cast<void*>(rgb_frame->GetData()), kCpuImageDefaultSaveQuality);
        if (!success) {
            return {};
        }
        return buffer;
    }

    VideoFramePtr VideoFrameProcCpu::DecodeJpeg(const std::vector<u_int8_t>& data) {
        if (data.empty()) {
            LOG_ERRO("{}", "DecodeJpeg() - empty data");
            return nullptr;
        }

        int w = 0, h = 0, channels = 0;
        std::unique_ptr<unsigned char, void (*)(void*)> img_guard(
            stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 3),
            stbi_image_free);
        if (!img_guard) {
            LOG_WARN("{}", "DecodeJpeg() - stbi_load_from_memory failed");
            return nullptr;
        }

        // CPU picture analysis keeps decoded images in packed BGR. This avoids
        // the RGB->I420->BGR round-trip that can corrupt x86 preview/inference frames.
        auto bgr_frame = std::make_shared<VideoFrame>(w, h, PixelFormat::PIXEL_BGR8);
        if (!VideoFrameValid(bgr_frame, true)) {
            return nullptr;
        }

        auto* src             = img_guard.get();
        auto* dst             = bgr_frame->GetData();
        const int pixel_count = w * h;
        for (int i = 0; i < pixel_count; i++) {
            int idx      = i * 3;
            dst[idx + 0] = src[idx + 2];
            dst[idx + 1] = src[idx + 1];
            dst[idx + 2] = src[idx + 0];
        }

        return bgr_frame;
    }

    // ========== Drawing implementations (CPU-native pixel ops) ==========

    namespace {
        // --- Format detection helpers ---
        inline bool IsPackedRgbFormat(PixelFormat pf) {
            return pf == PixelFormat::PIXEL_BGR8 || pf == PixelFormat::PIXEL_RGB8;
        }
        inline bool IsDrawableFormat(PixelFormat pf) {
            return pf == PixelFormat::PIXEL_I420 || IsPackedRgbFormat(pf);
        }

        // --- RGB to YUV conversion helpers (for I420 drawing) ---
        inline uint8_t RgbToY(uint8_t r, uint8_t g, uint8_t b) {
            return static_cast<uint8_t>(std::clamp(0.299f * r + 0.587f * g + 0.114f * b, 0.f, 255.f));
        }
        inline uint8_t RgbToU(uint8_t r, uint8_t g, uint8_t b) {
            return static_cast<uint8_t>(std::clamp(128.f - 0.169f * r - 0.331f * g + 0.500f * b, 0.f, 255.f));
        }
        inline uint8_t RgbToV(uint8_t r, uint8_t g, uint8_t b) {
            return static_cast<uint8_t>(std::clamp(128.f + 0.500f * r - 0.419f * g - 0.081f * b, 0.f, 255.f));
        }

        // --- I420 pixel operations ---
        inline void SetPixelI420(uint8_t* data, int w, int h, int px, int py, uint8_t yv, uint8_t uv,
                                 uint8_t vv) {
            if (px < 0 || px >= w || py < 0 || py >= h)
                return;
            data[py * w + px]                              = yv;  // Y plane
            int hw                                         = w / 2;
            int hx                                         = px / 2;
            int hy                                         = py / 2;
            data[w * h + hy * hw + hx]                     = uv;  // U plane
            data[w * h + (w / 2) * (h / 2) + hy * hw + hx] = vv;  // V plane
        }

        inline void DrawHLineI420(uint8_t* data, int w, int h, int x1, int x2, int y, int thickness,
                                  uint8_t yv, uint8_t uv, uint8_t vv) {
            for (int t = 0; t < thickness; t++) {
                int cy = y + t;
                if (cy < 0 || cy >= h)
                    continue;
                for (int x = std::max(0, x1); x <= std::min(x2, w - 1); x++) {
                    SetPixelI420(data, w, h, x, cy, yv, uv, vv);
                }
            }
        }

        inline void DrawVLineI420(uint8_t* data, int w, int h, int x, int y1, int y2, int thickness,
                                  uint8_t yv, uint8_t uv, uint8_t vv) {
            for (int t = 0; t < thickness; t++) {
                int cx = x + t;
                if (cx < 0 || cx >= w)
                    continue;
                for (int y = std::max(0, y1); y <= std::min(y2, h - 1); y++) {
                    SetPixelI420(data, w, h, cx, y, yv, uv, vv);
                }
            }
        }

        // --- Packed BGR/RGB pixel operations ---
        // Set a single pixel on packed 3-channel frame (BGR8 or RGB8)
        inline void SetPixelPacked(uint8_t* data, int w, int h, int px, int py, uint8_t ch0, uint8_t ch1,
                                   uint8_t ch2) {
            if (px < 0 || px >= w || py < 0 || py >= h)
                return;
            int idx       = (py * w + px) * 3;
            data[idx + 0] = ch0;
            data[idx + 1] = ch1;
            data[idx + 2] = ch2;
        }
    }  // namespace

    VideoFramePtr VideoFrameProcCpu::DrawBox(VideoFramePtr srcImage, const util::Box imageRect,
                                             const Color& color, int lineWidth) {
        if (!VideoFrameValid(srcImage, true))
            return srcImage;
        if (!IsDrawableFormat(srcImage->GetPixelFormat()))
            return srcImage;

        auto w     = static_cast<int>(srcImage->GetWidth());
        auto h     = static_cast<int>(srcImage->GetHeight());
        auto* data = srcImage->GetData();

        int x1 = imageRect.x;
        int y1 = imageRect.y;
        int x2 = imageRect.x + imageRect.width - 1;
        int y2 = imageRect.y + imageRect.height - 1;

        if (IsPackedRgbFormat(srcImage->GetPixelFormat())) {
            // BGR/RGB: determine channel order
            bool isBgr  = (srcImage->GetPixelFormat() == PixelFormat::PIXEL_BGR8);
            uint8_t ch0 = isBgr ? color.blue : color.red;
            uint8_t ch1 = color.green;
            uint8_t ch2 = isBgr ? color.red : color.blue;
            // Draw rectangle edges
            for (int t = 0; t < lineWidth; t++) {
                for (int x = std::max(0, x1); x <= std::min(x2, w - 1); x++) {
                    SetPixelPacked(data, w, h, x, y1 + t, ch0, ch1, ch2);
                    SetPixelPacked(data, w, h, x, y2 - t, ch0, ch1, ch2);
                }
                for (int y = std::max(0, y1); y <= std::min(y2, h - 1); y++) {
                    SetPixelPacked(data, w, h, x1 + t, y, ch0, ch1, ch2);
                    SetPixelPacked(data, w, h, x2 - t, y, ch0, ch1, ch2);
                }
            }
        } else {
            // I420
            uint8_t yv = RgbToY(color.red, color.green, color.blue);
            uint8_t uv = RgbToU(color.red, color.green, color.blue);
            uint8_t vv = RgbToV(color.red, color.green, color.blue);
            DrawHLineI420(data, w, h, x1, x2, y1, lineWidth, yv, uv, vv);
            DrawHLineI420(data, w, h, x1, x2, y2 - lineWidth + 1, lineWidth, yv, uv, vv);
            DrawVLineI420(data, w, h, x1, y1, y2, lineWidth, yv, uv, vv);
            DrawVLineI420(data, w, h, x2 - lineWidth + 1, y1, y2, lineWidth, yv, uv, vv);
        }

        return srcImage;
    }

    VideoFramePtr VideoFrameProcCpu::DrawPoint(VideoFramePtr srcImage, util::Point point, const Color& color,
                                               int lineWidth) {
        if (!VideoFrameValid(srcImage, true))
            return srcImage;
        if (!IsDrawableFormat(srcImage->GetPixelFormat()))
            return srcImage;

        auto w     = static_cast<int>(srcImage->GetWidth());
        auto h     = static_cast<int>(srcImage->GetHeight());
        auto* data = srcImage->GetData();
        int radius = lineWidth / 2;

        if (IsPackedRgbFormat(srcImage->GetPixelFormat())) {
            bool isBgr  = (srcImage->GetPixelFormat() == PixelFormat::PIXEL_BGR8);
            uint8_t ch0 = isBgr ? color.blue : color.red;
            uint8_t ch1 = color.green;
            uint8_t ch2 = isBgr ? color.red : color.blue;
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++)
                    SetPixelPacked(data, w, h, point.x + dx, point.y + dy, ch0, ch1, ch2);
        } else {
            uint8_t yv = RgbToY(color.red, color.green, color.blue);
            uint8_t uv = RgbToU(color.red, color.green, color.blue);
            uint8_t vv = RgbToV(color.red, color.green, color.blue);
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++)
                    SetPixelI420(data, w, h, point.x + dx, point.y + dy, yv, uv, vv);
        }
        return srcImage;
    }

    VideoFramePtr VideoFrameProcCpu::DrawLine(VideoFramePtr srcImage, util::Point point1, util::Point point2,
                                              const Color& color, int lineWidth) {
        return DrawLines(srcImage, {{point1, point2}}, color, lineWidth);
    }

    VideoFramePtr VideoFrameProcCpu::DrawLines(VideoFramePtr srcImage,
                                               std::vector<std::pair<util::Point, util::Point>> lines,
                                               const Color& color, int lineWidth) {
        if (!VideoFrameValid(srcImage, true))
            return srcImage;
        if (!IsDrawableFormat(srcImage->GetPixelFormat()))
            return srcImage;

        auto w     = static_cast<int>(srcImage->GetWidth());
        auto h     = static_cast<int>(srcImage->GetHeight());
        auto* data = srcImage->GetData();
        int half   = lineWidth / 2;

        bool packed = IsPackedRgbFormat(srcImage->GetPixelFormat());
        // Precompute color values for both paths
        bool isBgr  = (srcImage->GetPixelFormat() == PixelFormat::PIXEL_BGR8);
        uint8_t ch0 = packed ? (isBgr ? color.blue : color.red) : 0;
        uint8_t ch1 = color.green;
        uint8_t ch2 = packed ? (isBgr ? color.red : color.blue) : 0;
        uint8_t yv = 0, uv = 0, vv = 0;
        if (!packed) {
            yv = RgbToY(color.red, color.green, color.blue);
            uv = RgbToU(color.red, color.green, color.blue);
            vv = RgbToV(color.red, color.green, color.blue);
        }

        for (auto& [p1, p2] : lines) {
            // Bresenham's line algorithm with thickness
            int x0 = p1.x, y0 = p1.y, x1 = p2.x, y1 = p2.y;
            int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
            int sx  = (x0 < x1) ? 1 : -1;
            int sy  = (y0 < y1) ? 1 : -1;
            int err = dx - dy;

            while (true) {
                for (int ty = -half; ty <= half; ty++) {
                    for (int tx = -half; tx <= half; tx++) {
                        if (packed)
                            SetPixelPacked(data, w, h, x0 + tx, y0 + ty, ch0, ch1, ch2);
                        else
                            SetPixelI420(data, w, h, x0 + tx, y0 + ty, yv, uv, vv);
                    }
                }

                if (x0 == x1 && y0 == y1)
                    break;
                int e2 = 2 * err;
                if (e2 > -dy) {
                    err -= dy;
                    x0 += sx;
                }
                if (e2 < dx) {
                    err += dx;
                    y0 += sy;
                }
            }
        }
        return srcImage;
    }

    VideoFramePtr VideoFrameProcCpu::DrawRects(VideoFramePtr srcImage, const std::vector<util::Box>& rects,
                                               const Color& color, int lineWidth) {
        for (auto& r : rects) {
            srcImage = DrawBox(srcImage, r, color, lineWidth);
        }
        return srcImage;
    }

    VideoFramePtr VideoFrameProcCpu::DrawText(VideoFramePtr srcImage, int x, int y, const std::string& text,
                                              const Color& color, int fontSize) {
        if (!VideoFrameValid(srcImage, true))
            return srcImage;
        if (!IsDrawableFormat(srcImage->GetPixelFormat()))
            return srcImage;
        if (!osd_service_.IsReady())
            return srcImage;

        auto bitmap = osd_service_.RenderString(text, static_cast<float>(fontSize));
        if (bitmap.alpha.empty())
            return srcImage;

        auto w     = static_cast<int>(srcImage->GetWidth());
        auto h     = static_cast<int>(srcImage->GetHeight());
        auto* data = srcImage->GetData();

        if (IsPackedRgbFormat(srcImage->GetPixelFormat())) {
            bool isBgr  = (srcImage->GetPixelFormat() == PixelFormat::PIXEL_BGR8);
            uint8_t tc0 = isBgr ? color.blue : color.red;
            uint8_t tc1 = color.green;
            uint8_t tc2 = isBgr ? color.red : color.blue;
            for (int by = 0; by < bitmap.height; by++) {
                int py = y + by;
                if (py < 0 || py >= h)
                    continue;
                for (int bx = 0; bx < bitmap.width; bx++) {
                    int px = x + bx;
                    if (px < 0 || px >= w)
                        continue;
                    uint8_t alpha = bitmap.alpha[by * bitmap.width + bx];
                    if (alpha == 0)
                        continue;
                    float a       = alpha / 255.0f;
                    int idx       = (py * w + px) * 3;
                    data[idx + 0] = static_cast<uint8_t>(data[idx + 0] * (1 - a) + tc0 * a);
                    data[idx + 1] = static_cast<uint8_t>(data[idx + 1] * (1 - a) + tc1 * a);
                    data[idx + 2] = static_cast<uint8_t>(data[idx + 2] * (1 - a) + tc2 * a);
                }
            }
        } else {
            uint8_t yv = RgbToY(color.red, color.green, color.blue);
            uint8_t uv = RgbToU(color.red, color.green, color.blue);
            uint8_t vv = RgbToV(color.red, color.green, color.blue);
            for (int by = 0; by < bitmap.height; by++) {
                int py = y + by;
                if (py < 0 || py >= h)
                    continue;
                for (int bx = 0; bx < bitmap.width; bx++) {
                    int px = x + bx;
                    if (px < 0 || px >= w)
                        continue;
                    uint8_t alpha = bitmap.alpha[by * bitmap.width + bx];
                    if (alpha == 0)
                        continue;
                    float a  = alpha / 255.0f;
                    int yi   = py * w + px;
                    data[yi] = static_cast<uint8_t>(data[yi] * (1 - a) + yv * a);
                    int hw   = w / 2;
                    int hx   = px / 2;
                    int hy   = py / 2;
                    int ui   = w * h + hy * hw + hx;
                    int vi   = w * h + hw * (h / 2) + hy * hw + hx;
                    data[ui] = static_cast<uint8_t>(data[ui] * (1 - a) + uv * a);
                    data[vi] = static_cast<uint8_t>(data[vi] * (1 - a) + vv * a);
                }
            }
        }

        return srcImage;
    }

    // ========== Session-based OSD API ==========

    bool VideoFrameProcCpu::BeginOSD(VideoFramePtr frame) {
        if (!VideoFrameValid(frame, true))
            return false;
        if (!IsDrawableFormat(frame->GetPixelFormat()))
            return false;
        osd_frame_ = frame;
        return true;
    }

    void VideoFrameProcCpu::OSDDrawLines(std::vector<std::pair<util::Point, util::Point>> lines,
                                         const Color& color, int lineWidth) {
        if (osd_frame_)
            DrawLines(osd_frame_, std::move(lines), color, lineWidth);
    }

    void VideoFrameProcCpu::OSDDrawText(int x, int y, const std::string& text, const Color& color,
                                        int fontSize) {
        if (osd_frame_)
            DrawText(osd_frame_, x, y, text, color, fontSize);
    }

    void VideoFrameProcCpu::OSDDrawTextEx(int x, int y, const std::string& text, const Color& color,
                                          int fontSize, const Color& bgColor, uint8_t bgAlpha, bool outline,
                                          int bgPadding) {
        if (!osd_frame_)
            return;
        if (!osd_service_.IsReady())
            return;

        auto w     = static_cast<int>(osd_frame_->GetWidth());
        auto h     = static_cast<int>(osd_frame_->GetHeight());
        auto* data = osd_frame_->GetData();
        auto pf    = osd_frame_->GetPixelFormat();

        if (IsPackedRgbFormat(pf)) {
            const bool is_bgr = (pf == PixelFormat::PIXEL_BGR8);
            auto blend_packed = [&](int px, int py, const Color& c, float alpha) {
                if (px < 0 || px >= w || py < 0 || py >= h) {
                    return;
                }
                int idx       = (py * w + px) * 3;
                uint8_t ch0   = is_bgr ? c.blue : c.red;
                uint8_t ch1   = c.green;
                uint8_t ch2   = is_bgr ? c.red : c.blue;
                data[idx + 0] = static_cast<uint8_t>(data[idx + 0] * (1.0f - alpha) + ch0 * alpha);
                data[idx + 1] = static_cast<uint8_t>(data[idx + 1] * (1.0f - alpha) + ch1 * alpha);
                data[idx + 2] = static_cast<uint8_t>(data[idx + 2] * (1.0f - alpha) + ch2 * alpha);
            };

            if (outline) {
                auto bmp = osd_service_.RenderStringWithOutline(text, static_cast<float>(fontSize));
                if (bmp.bodyAlpha.empty()) {
                    return;
                }

                if (bgAlpha > 0) {
                    float ba = bgAlpha / 255.0f;
                    int bx1  = x - bmp.offsetX - bgPadding;
                    int by1  = y - bmp.offsetY - bgPadding;
                    int bx2  = bx1 + bmp.width + 2 * bgPadding;
                    int by2  = by1 + bmp.height + 2 * bgPadding;

                    for (int py = std::max(0, by1); py < std::min(h, by2); py++) {
                        for (int px = std::max(0, bx1); px < std::min(w, bx2); px++) {
                            blend_packed(px, py, bgColor, ba);
                        }
                    }
                }

                for (int by = 0; by < bmp.height; by++) {
                    int py = y - bmp.offsetY + by;
                    if (py < 0 || py >= h) {
                        continue;
                    }
                    for (int bx = 0; bx < bmp.width; bx++) {
                        int px = x - bmp.offsetX + bx;
                        if (px < 0 || px >= w) {
                            continue;
                        }
                        uint8_t oa = bmp.outlineAlpha[by * bmp.width + bx];
                        if (oa != 0) {
                            blend_packed(px, py, {0, 0, 0}, oa / 255.0f);
                        }
                    }
                }

                for (int by = 0; by < bmp.height; by++) {
                    int py = y - bmp.offsetY + by;
                    if (py < 0 || py >= h) {
                        continue;
                    }
                    for (int bx = 0; bx < bmp.width; bx++) {
                        int px = x - bmp.offsetX + bx;
                        if (px < 0 || px >= w) {
                            continue;
                        }
                        uint8_t ba = bmp.bodyAlpha[by * bmp.width + bx];
                        if (ba != 0) {
                            blend_packed(px, py, color, ba / 255.0f);
                        }
                    }
                }
            } else {
                DrawText(osd_frame_, x, y, text, color, fontSize);
            }
            return;
        }

        if (outline) {
            auto bmp = osd_service_.RenderStringWithOutline(text, static_cast<float>(fontSize));
            if (bmp.bodyAlpha.empty())
                return;

            // Draw background rectangle
            if (bgAlpha > 0) {
                uint8_t byv = RgbToY(bgColor.red, bgColor.green, bgColor.blue);
                uint8_t buv = RgbToU(bgColor.red, bgColor.green, bgColor.blue);
                uint8_t bvv = RgbToV(bgColor.red, bgColor.green, bgColor.blue);
                float ba    = bgAlpha / 255.0f;

                int bx1 = x - bmp.offsetX - bgPadding;
                int by1 = y - bmp.offsetY - bgPadding;
                int bx2 = bx1 + bmp.width + 2 * bgPadding;
                int by2 = by1 + bmp.height + 2 * bgPadding;

                for (int py = std::max(0, by1); py < std::min(h, by2); py++) {
                    for (int px = std::max(0, bx1); px < std::min(w, bx2); px++) {
                        int yi   = py * w + px;
                        data[yi] = static_cast<uint8_t>(data[yi] * (1 - ba) + byv * ba);
                        int hw   = w / 2;
                        int hx   = px / 2;
                        int hy   = py / 2;
                        int ui   = w * h + hy * hw + hx;
                        int vi   = w * h + hw * (h / 2) + hy * hw + hx;
                        data[ui] = static_cast<uint8_t>(data[ui] * (1 - ba) + buv * ba);
                        data[vi] = static_cast<uint8_t>(data[vi] * (1 - ba) + bvv * ba);
                    }
                }
            }

            // Draw outline (black)
            uint8_t oyv = RgbToY(0, 0, 0);
            uint8_t ouv = RgbToU(0, 0, 0);
            uint8_t ovv = RgbToV(0, 0, 0);
            for (int by = 0; by < bmp.height; by++) {
                int py = y - bmp.offsetY + by;
                if (py < 0 || py >= h)
                    continue;
                for (int bx = 0; bx < bmp.width; bx++) {
                    int px = x - bmp.offsetX + bx;
                    if (px < 0 || px >= w)
                        continue;
                    uint8_t oa = bmp.outlineAlpha[by * bmp.width + bx];
                    if (oa == 0)
                        continue;
                    float a  = oa / 255.0f;
                    int yi   = py * w + px;
                    data[yi] = static_cast<uint8_t>(data[yi] * (1 - a) + oyv * a);
                }
            }

            // Draw body text
            uint8_t tyv = RgbToY(color.red, color.green, color.blue);
            uint8_t tuv = RgbToU(color.red, color.green, color.blue);
            uint8_t tvv = RgbToV(color.red, color.green, color.blue);
            for (int by = 0; by < bmp.height; by++) {
                int py = y - bmp.offsetY + by;
                if (py < 0 || py >= h)
                    continue;
                for (int bx = 0; bx < bmp.width; bx++) {
                    int px = x - bmp.offsetX + bx;
                    if (px < 0 || px >= w)
                        continue;
                    uint8_t ba = bmp.bodyAlpha[by * bmp.width + bx];
                    if (ba == 0)
                        continue;
                    float a  = ba / 255.0f;
                    int yi   = py * w + px;
                    data[yi] = static_cast<uint8_t>(data[yi] * (1 - a) + tyv * a);
                    int hw   = w / 2;
                    int hx   = px / 2;
                    int hy   = py / 2;
                    int ui   = w * h + hy * hw + hx;
                    int vi   = w * h + hw * (h / 2) + hy * hw + hx;
                    data[ui] = static_cast<uint8_t>(data[ui] * (1 - a) + tuv * a);
                    data[vi] = static_cast<uint8_t>(data[vi] * (1 - a) + tvv * a);
                }
            }
        } else {
            DrawText(osd_frame_, x, y, text, color, fontSize);
        }
    }

    void VideoFrameProcCpu::EndOSD() {
        osd_frame_ = nullptr;
    }

    bool VideoFrameProcCpu::InitTextRenderer(const std::string& fontPath) {
        return osd_service_.Init(fontPath);
    }

}  // namespace media
}  // namespace cosmo
