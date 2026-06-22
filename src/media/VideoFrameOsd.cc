// VideoFrameOsd.cc — Session-based OSD drawing and TrueType text rendering.
// Split from VideoFrameDrawing.cc to reduce file size (Phase 2 file split).

#include <vector>

#include "bmcv_api_ext.h"
#include "media/IOsdTextRenderer.h"
#include "media/OsdTextRenderer.h"
#include "media/PixelFormatUtils.h"
#include "media/VideoFrameProcSophon.h"
#include "util/Log.h"

#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#define HAS_NEON 1
#else
#define HAS_NEON 0
#endif

namespace cosmo {
namespace media {

    // ── NEON accelerated alpha blend ──
    // dst[i] = (alpha[i] * color + (255 - alpha[i]) * dst[i]) / 255
    static inline void BlendRowAlpha(uint8_t* dst, const uint8_t* alpha, uint8_t color, int width) {
#if HAS_NEON
        int i            = 0;
        uint8x8_t vcolor = vdup_n_u8(color);
        uint8x8_t v255   = vdup_n_u8(255);
        for (; i + 8 <= width; i += 8) {
            uint8x8_t va     = vld1_u8(alpha + i);
            uint8x8_t vd     = vld1_u8(dst + i);
            uint16x8_t prod1 = vmull_u8(va, vcolor);
            uint8x8_t inv_a  = vsub_u8(v255, va);
            uint16x8_t prod2 = vmull_u8(inv_a, vd);
            uint16x8_t sum   = vaddq_u16(prod1, prod2);
            // /255 approximation: (x + 128) >> 8
            sum              = vaddq_u16(sum, vdupq_n_u16(128));
            uint8x8_t result = vshrn_n_u16(sum, 8);
            vst1_u8(dst + i, result);
        }
        for (; i < width; ++i) {
            uint8_t a = alpha[i];
            if (a == 0)
                continue;
            dst[i] = static_cast<uint8_t>((a * color + (255 - a) * dst[i]) / 255);
        }
#else
        for (int i = 0; i < width; ++i) {
            uint8_t a = alpha[i];
            if (a == 0)
                continue;
            dst[i] = static_cast<uint8_t>((a * color + (255 - a) * dst[i]) / 255);
        }
#endif
    }

    // ===== Session-based OSD API =====

    static thread_local VideoFrameProcSophon::BmFrameImagePtr t_osdImage = nullptr;
    static thread_local int t_osdWidth                                   = 0;
    static thread_local int t_osdHeight                                  = 0;

    /// Text draw queue entry
    struct OsdQueuedText {
        int x, y;
        std::string text;
        Color color;
        int fontSize;
        // Phase2: semi-transparent background + outline
        bool hasBg{false};
        Color bgColor{0, 0, 0};
        uint8_t bgAlpha{0};  // 0~255
        bool outline{false};
        int bgPadding{4};  // background padding (px)
    };
    static thread_local std::vector<OsdQueuedText> t_osdTexts;

    bool VideoFrameProcSophon::InitTextRenderer(const std::string& fontPath) {
        return osd_service_.Init(fontPath);
    }

    bool VideoFrameProcSophon::BeginOSD(VideoFramePtr frame) {
        if (t_osdImage) {
            LOG_WARN("{}", "BeginOSD() - previous session not ended, cleaning up");
            t_osdImage = nullptr;
        }
        t_osdTexts.clear();

        if (!VideoFrameValid(frame, true)) {
            return false;
        }

        if (frame->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "BeginOSD() - FrameType Not Support");
            return false;
        }

        auto width  = frame->GetWidth();
        auto height = frame->GetHeight();

        t_osdImage = CreateBMImage(width, height, frame->GetPixelFormat());
        if (!t_osdImage) {
            LOG_WARN("{}", "BeginOSD() - CreateBMImage failed");
            return false;
        }

        if (!VideoFrameAttach(frame, t_osdImage.get())) {
            LOG_WARN("{}", "BeginOSD() - VideoFrameAttach failed");
            t_osdImage = nullptr;
            return false;
        }

        t_osdWidth  = static_cast<int>(width);
        t_osdHeight = static_cast<int>(height);
        return true;
    }

    void VideoFrameProcSophon::OSDDrawLines(std::vector<std::pair<util::Point, util::Point>> lines,
                                            const Color& color, int lineWidth) {
        if (!t_osdImage) {
            return;
        }

        constexpr size_t lineSize = 128;
        bmcv_point_t start[lineSize];
        bmcv_point_t end[lineSize];

        size_t lineCount = 0;
        for (auto& line : lines) {
            if (lineCount >= lineSize) {
                break;
            }
            if ((line.first.x < 0) || (line.first.y < 0) || (line.second.x < 0) || (line.second.y < 0)) {
                continue;
            }
            start[lineCount].x = line.first.x;
            start[lineCount].y = line.first.y;
            end[lineCount].x   = line.second.x;
            end[lineCount].y   = line.second.y;
            lineCount += 1;
        }

        if (lineCount == 0 || lineCount > lineSize) {
            return;
        }

        bmcv_color_t c;
        c.r = color.red;
        c.g = color.green;
        c.b = color.blue;

        auto ret =
            bmcv_image_draw_lines(handle, *t_osdImage, start, end, static_cast<int>(lineCount), c, lineWidth);
        if (ret != BM_SUCCESS) {
            LOG_WARN("OSDDrawLines - bmcv_image_draw_lines failed {}", ret);
        }
    }

    void VideoFrameProcSophon::OSDDrawText(int x, int y, const std::string& text, const Color& color,
                                           int fontSize) {
        if (!t_osdImage) {
            return;
        }
        if (x < 0 || y < 0 || x >= t_osdWidth || y >= t_osdHeight) {
            return;
        }
        // Enqueue, batch render at EndOSD
        t_osdTexts.push_back({x, y, text, color, fontSize, false, {0, 0, 0}, 0, false, 4});
    }

    void VideoFrameProcSophon::OSDDrawTextEx(int x, int y, const std::string& text, const Color& color,
                                             int fontSize, const Color& bgColor, uint8_t bgAlpha,
                                             bool outline, int bgPadding) {
        if (!t_osdImage) {
            return;
        }
        if (x < 0 || y < 0 || x >= t_osdWidth || y >= t_osdHeight) {
            return;
        }
        t_osdTexts.push_back({x, y, text, color, fontSize, true, bgColor, bgAlpha, outline, bgPadding});
    }

    void VideoFrameProcSophon::EndOSD() {
        if (t_osdImage && !t_osdTexts.empty()) {
            FlushTextQueue();
        }
        t_osdTexts.clear();
        if (t_osdImage) {
            t_osdImage = nullptr;
        }
    }

    void VideoFrameProcSophon::FlushTextQueue() {
        auto& renderer = osd_service_;
        if (!renderer.IsReady()) {
            // TrueType not initialized, fallback to bmcv native rendering
            for (auto& qt : t_osdTexts) {
                bmcv_point_t point{qt.x, qt.y};
                bmcv_color_t c{qt.color.red, qt.color.green, qt.color.blue};
                float fontScale = static_cast<float>(qt.fontSize) / 10.0f;
                bmcv_image_put_text(handle, *t_osdImage, qt.text.c_str(), point, c, fontScale, 0);
            }
            return;
        }

        // 1. Reuse thread_local I420 host buffer (avoid per-frame malloc 3MB+)
        size_t yPlaneSize  = static_cast<size_t>(t_osdWidth) * static_cast<size_t>(t_osdHeight);
        size_t uvPlaneSize = static_cast<size_t>(t_osdWidth / 2) * static_cast<size_t>(t_osdHeight / 2);
        static thread_local std::vector<uint8_t> yBuf, uBuf, vBuf;
        yBuf.resize(yPlaneSize);
        uBuf.resize(uvPlaneSize);
        vBuf.resize(uvPlaneSize);

        void* hostPtrs[3] = {yBuf.data(), uBuf.data(), vBuf.data()};

        // 2. Copy from device memory to host
        auto ret = bm_image_copy_device_to_host(*t_osdImage, hostPtrs);
        if (ret != BM_SUCCESS) {
            LOG_WARN("FlushTextQueue - bm_image_copy_device_to_host failed {}", ret);
            return;
        }

        int uvStride = t_osdWidth / 2;

        // 3. Render each text entry using pre-baked outline + NEON accelerated blending
        for (auto& qt : t_osdTexts) {
            float pixelHeight = static_cast<float>(qt.fontSize) * 1.5f;

            // RGB to YUV color conversion (text color)
            float rf = qt.color.red, gf = qt.color.green, bf = qt.color.blue;
            auto yc = static_cast<uint8_t>(std::clamp(0.299f * rf + 0.587f * gf + 0.114f * bf, 0.f, 255.f));
            auto uc =
                static_cast<uint8_t>(std::clamp(128.f - 0.169f * rf - 0.331f * gf + 0.500f * bf, 0.f, 255.f));
            auto vc =
                static_cast<uint8_t>(std::clamp(128.f + 0.500f * rf - 0.419f * gf - 0.081f * bf, 0.f, 255.f));

            if (qt.outline) {
                // Fast path: pre-baked outline + body, 2 row traversals instead of 5
                auto ob = renderer.RenderStringWithOutline(qt.text, pixelHeight);
                if (ob.outlineAlpha.empty())
                    continue;

                int drawX = qt.x - ob.offsetX;
                int drawY = qt.y - ob.offsetY;

                // Pass A: semi-transparent background rectangle
                if (qt.hasBg && qt.bgAlpha > 0) {
                    float bgRf = qt.bgColor.red, bgGf = qt.bgColor.green, bgBf = qt.bgColor.blue;
                    auto bgYc = static_cast<uint8_t>(
                        std::clamp(0.299f * bgRf + 0.587f * bgGf + 0.114f * bgBf, 0.f, 255.f));
                    auto bgUc = static_cast<uint8_t>(
                        std::clamp(128.f - 0.169f * bgRf - 0.331f * bgGf + 0.500f * bgBf, 0.f, 255.f));
                    auto bgVc = static_cast<uint8_t>(
                        std::clamp(128.f + 0.500f * bgRf - 0.419f * bgGf - 0.081f * bgBf, 0.f, 255.f));
                    int bgX0    = std::max(0, drawX - qt.bgPadding);
                    int bgY0    = std::max(0, drawY - qt.bgPadding);
                    int bgX1    = std::min(t_osdWidth, drawX + ob.width + qt.bgPadding);
                    int bgY1    = std::min(t_osdHeight, drawY + ob.height + qt.bgPadding);
                    uint8_t bgA = qt.bgAlpha;
                    static thread_local std::vector<uint8_t> bgAlphaRow;
                    int bgW = bgX1 - bgX0;
                    if (bgW > 0) {
                        bgAlphaRow.assign(static_cast<size_t>(bgW), bgA);
                        for (int py = bgY0; py < bgY1; ++py) {
                            BlendRowAlpha(yBuf.data() + static_cast<size_t>(py * t_osdWidth + bgX0),
                                          bgAlphaRow.data(), bgYc, bgW);
                            if ((py & 1) == 0) {
                                int uvY  = py / 2;
                                int uvX0 = bgX0 / 2;
                                int uvW  = (bgX1 / 2) - uvX0;
                                if (uvW > 0) {
                                    bgAlphaRow.resize(static_cast<size_t>(uvW));
                                    std::fill(bgAlphaRow.begin(), bgAlphaRow.begin() + uvW, bgA);
                                    BlendRowAlpha(uBuf.data() + static_cast<size_t>(uvY * uvStride + uvX0),
                                                  bgAlphaRow.data(), bgUc, uvW);
                                    BlendRowAlpha(vBuf.data() + static_cast<size_t>(uvY * uvStride + uvX0),
                                                  bgAlphaRow.data(), bgVc, uvW);
                                    bgAlphaRow.assign(static_cast<size_t>(bgW), bgA);
                                }
                            }
                        }
                    }
                }

                // Pass B+C merged: dark outline for contrast, then colored body
                uint8_t outYc = 16, outUc = 128, outVc = 128;
                for (int by = 0; by < ob.height; ++by) {
                    int fy = drawY + by;
                    if (fy < 0 || fy >= t_osdHeight)
                        continue;
                    int rowStart = by * ob.width;

                    int fxStart = std::max(0, drawX);
                    int fxEnd   = std::min(t_osdWidth, drawX + ob.width);
                    int bxStart = fxStart - drawX;
                    int bxEnd   = fxEnd - drawX;
                    int rowW    = bxEnd - bxStart;
                    if (rowW <= 0)
                        continue;

                    BlendRowAlpha(yBuf.data() + static_cast<size_t>(fy * t_osdWidth + fxStart),
                                  ob.outlineAlpha.data() + static_cast<size_t>(rowStart + bxStart), outYc,
                                  rowW);
                    BlendRowAlpha(yBuf.data() + static_cast<size_t>(fy * t_osdWidth + fxStart),
                                  ob.bodyAlpha.data() + static_cast<size_t>(rowStart + bxStart), yc, rowW);

                    if ((fy & 1) == 0) {
                        int uvY       = fy / 2;
                        int uvFxStart = fxStart / 2;
                        int uvFxEnd   = fxEnd / 2;
                        for (int uvx = uvFxStart; uvx < uvFxEnd; ++uvx) {
                            int bx = (uvx * 2) - drawX;
                            if (bx < 0 || bx >= ob.width)
                                continue;
                            uint8_t oa   = ob.outlineAlpha[static_cast<size_t>(rowStart + bx)];
                            uint8_t ba   = ob.bodyAlpha[static_cast<size_t>(rowStart + bx)];
                            size_t uvIdx = static_cast<size_t>(uvY * uvStride + uvx);
                            if (oa > 0) {
                                uBuf[uvIdx] =
                                    static_cast<uint8_t>((oa * outUc + (255 - oa) * uBuf[uvIdx]) / 255);
                                vBuf[uvIdx] =
                                    static_cast<uint8_t>((oa * outVc + (255 - oa) * vBuf[uvIdx]) / 255);
                            }
                            if (ba > 0) {
                                uBuf[uvIdx] =
                                    static_cast<uint8_t>((ba * uc + (255 - ba) * uBuf[uvIdx]) / 255);
                                vBuf[uvIdx] =
                                    static_cast<uint8_t>((ba * vc + (255 - ba) * vBuf[uvIdx]) / 255);
                            }
                        }
                    }
                }
            } else {
                // No outline: simple path
                auto bitmap = renderer.RenderString(qt.text, pixelHeight);
                if (bitmap.alpha.empty())
                    continue;

                // Pass A: semi-transparent background
                if (qt.hasBg && qt.bgAlpha > 0) {
                    float bgRf = qt.bgColor.red, bgGf = qt.bgColor.green, bgBf = qt.bgColor.blue;
                    auto bgYc = static_cast<uint8_t>(
                        std::clamp(0.299f * bgRf + 0.587f * bgGf + 0.114f * bgBf, 0.f, 255.f));
                    int bgX0    = std::max(0, qt.x - qt.bgPadding);
                    int bgY0    = std::max(0, qt.y - qt.bgPadding);
                    int bgX1    = std::min(t_osdWidth, qt.x + bitmap.width + qt.bgPadding);
                    int bgY1    = std::min(t_osdHeight, qt.y + bitmap.height + qt.bgPadding);
                    uint8_t bgA = qt.bgAlpha;
                    for (int py = bgY0; py < bgY1; ++py) {
                        for (int px = bgX0; px < bgX1; ++px) {
                            size_t yIdx = static_cast<size_t>(py * t_osdWidth + px);
                            yBuf[yIdx]  = static_cast<uint8_t>((bgA * bgYc + (255 - bgA) * yBuf[yIdx]) / 255);
                        }
                    }
                }

                // Pass C: body text (NEON accelerated)
                for (int by = 0; by < bitmap.height; ++by) {
                    int fy = qt.y + by;
                    if (fy < 0 || fy >= t_osdHeight)
                        continue;
                    int fxStart = std::max(0, qt.x);
                    int fxEnd   = std::min(t_osdWidth, qt.x + bitmap.width);
                    int bxStart = fxStart - qt.x;
                    int rowW    = fxEnd - fxStart;
                    if (rowW <= 0)
                        continue;

                    BlendRowAlpha(yBuf.data() + static_cast<size_t>(fy * t_osdWidth + fxStart),
                                  bitmap.alpha.data() + static_cast<size_t>(by * bitmap.width + bxStart), yc,
                                  rowW);
                    if ((fy & 1) == 0) {
                        int uvY = fy / 2;
                        for (int bx = bxStart; bx < bxStart + rowW; ++bx) {
                            int fx = qt.x + bx;
                            if ((fx & 1) != 0)
                                continue;
                            uint8_t a = bitmap.alpha[static_cast<size_t>(by * bitmap.width + bx)];
                            if (a == 0)
                                continue;
                            size_t uvIdx = static_cast<size_t>(uvY * uvStride + (fx / 2));
                            uBuf[uvIdx]  = static_cast<uint8_t>((a * uc + (255 - a) * uBuf[uvIdx]) / 255);
                            vBuf[uvIdx]  = static_cast<uint8_t>((a * vc + (255 - a) * vBuf[uvIdx]) / 255);
                        }
                    }
                }
            }
        }

        // 4. Write back to device memory
        ret = bm_image_copy_host_to_device(*t_osdImage, hostPtrs);
        if (ret != BM_SUCCESS) {
            LOG_WARN("FlushTextQueue - bm_image_copy_host_to_device failed {}", ret);
        }
    }

}  // namespace media
}  // namespace cosmo
