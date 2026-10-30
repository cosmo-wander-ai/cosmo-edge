// VideoFrameOsd.cc — Session-based OSD drawing and TrueType text rendering.
// Split from VideoFrameDrawing.cc to reduce file size (Phase 2 file split).

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <vector>

#include "bmcv_api_ext.h"
#include "media/IOsdTextRenderer.h"
#include "media/OsdLineRasterizer.h"
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

    static void ReportOsdException(const char* stage, const char* detail) noexcept {
        try {
            LOG_ERRO("OSD {} failed: {}; overlay may be incomplete", stage, detail);
        } catch (...) {
            // Logging may itself allocate during memory exhaustion. EndOSD
            // must still release the session and keep the engine alive.
        }
    }

    // Clip fallback lines to the valid frame interior without inventing border
    // edges; input subtraction happens in double to avoid signed overflow.
    static bool ClipNativeLine(util::Point& a, util::Point& b, int width, int height) {
        const double x0 = a.x, y0 = a.y;
        const double dx = static_cast<double>(b.x) - a.x;
        const double dy = static_cast<double>(b.y) - a.y;
        double first = 0.0, last = 1.0;
        const auto update = [&](double p, double q) {
            if (p == 0.0)
                return q >= 0.0;
            const double r = q / p;
            if (p < 0)
                first = std::max(first, r);
            else
                last = std::min(last, r);
            return first <= last;
        };
        if (!update(-dx, x0) || !update(dx, width - 1.0 - x0) || !update(-dy, y0) ||
            !update(dy, height - 1.0 - y0))
            return false;
        a.x = std::clamp(static_cast<int>(std::lround(x0 + first * dx)), 0, width - 1);
        a.y = std::clamp(static_cast<int>(std::lround(y0 + first * dy)), 0, height - 1);
        b.x = std::clamp(static_cast<int>(std::lround(x0 + last * dx)), 0, width - 1);
        b.y = std::clamp(static_cast<int>(std::lround(y0 + last * dy)), 0, height - 1);
        return true;
    }

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
    static thread_local VideoFramePtr t_osdFrame;
    static thread_local int t_osdWidth  = 0;
    static thread_local int t_osdHeight = 0;

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
        bool fine{false};  // actual-pixel regular-weight label text
    };
    static thread_local std::vector<OsdQueuedText> t_osdTexts;
    static thread_local std::vector<OsdLineGroup> t_osdLines;
    struct OsdQueuedRect {
        util::Box rect;
        Color color;
        uint8_t opacity;
        int radius{0};
        bool outline{false};
    };
    static thread_local std::vector<OsdQueuedRect> t_osdRects;

    bool VideoFrameProcSophon::InitTextRenderer(const std::string& fontPath) {
        return osd_service_.Init(fontPath);
    }

    bool VideoFrameProcSophon::BeginOSD(VideoFramePtr frame) {
        if (t_osdFrame) {
            LOG_WARN("{}", "BeginOSD() - previous session not ended, cleaning up");
            t_osdImage = nullptr;
            t_osdFrame = nullptr;
        }
        t_osdTexts.clear();
        t_osdLines.clear();
        t_osdRects.clear();

        if (!VideoFrameValid(frame, true)) {
            return false;
        }

        if (frame->GetPixelFormat() != PixelFormat::PIXEL_I420) {
            LOG_ERRO("{}", "BeginOSD() - FrameType Not Support");
            return false;
        }

        auto width  = frame->GetWidth();
        auto height = frame->GetHeight();
        if (width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            height > static_cast<size_t>(std::numeric_limits<int>::max())) {
            LOG_ERRO("{}", "BeginOSD() - dimensions exceed SDK limits");
            return false;
        }
        const auto expectedSize = PixelFormatUtils::CalculateFrameSize(
            static_cast<int>(width), static_cast<int>(height), PixelFormat::PIXEL_I420);
        auto* deviceMemory = reinterpret_cast<bm_mem_desc_t*>(frame->GetData());
        if (!expectedSize || *expectedSize != frame->GetSize() ||
            *expectedSize > std::numeric_limits<unsigned int>::max() || !deviceMemory ||
            bm_mem_get_device_size(*deviceMemory) < *expectedSize) {
            LOG_ERRO("{}", "BeginOSD() - invalid or undersized I420 device frame");
            return false;
        }

        t_osdWidth  = static_cast<int>(width);
        t_osdHeight = static_cast<int>(height);
        t_osdFrame  = std::move(frame);
        return true;
    }

    void VideoFrameProcSophon::OSDDrawLines(std::vector<std::pair<util::Point, util::Point>> lines,
                                            const Color& color, int lineWidth) {
        if (!t_osdFrame) {
            return;
        }

        if (lines.empty() || lineWidth <= 0) {
            return;
        }
        // Queue all segments; lines and text share the same host round-trip.
        t_osdLines.push_back({std::move(lines), color, lineWidth});
    }

    void VideoFrameProcSophon::OSDDrawText(int x, int y, const std::string& text, const Color& color,
                                           int fontSize) {
        if (!t_osdFrame) {
            return;
        }
        if (x < 0 || y < 0 || x >= t_osdWidth || y >= t_osdHeight) {
            return;
        }
        // Enqueue, batch render at EndOSD
        t_osdTexts.push_back({x, y, text, color, fontSize, false, {0, 0, 0}, 0, false, 4});
    }

    void VideoFrameProcSophon::OSDDrawLinesFine(std::vector<std::pair<util::Point, util::Point>> lines,
                                                const Color& color, int lineWidth) {
        if (t_osdFrame && !lines.empty() && lineWidth > 0)
            t_osdLines.push_back({std::move(lines), color, lineWidth, true});
    }

    void VideoFrameProcSophon::OSDDrawRoundedRect(const util::Box& rect, const Color& color, uint8_t opacity,
                                                  int radius) {
        if (t_osdFrame && rect.width > 0 && rect.height > 0 && opacity > 0)
            t_osdRects.push_back({rect, color, opacity, radius});
    }

    void VideoFrameProcSophon::OSDDrawRoundedRectOutline(const util::Box& rect, const Color& color,
                                                         uint8_t opacity, int radius) {
        if (t_osdFrame && rect.width > 0 && rect.height > 0 && opacity > 0)
            t_osdRects.push_back({rect, color, opacity, radius, true});
    }

    void VideoFrameProcSophon::OSDDrawTextFine(int x, int y, const std::string& text, const Color& color,
                                               int pixelHeight) {
        if (t_osdFrame && !text.empty() && pixelHeight > 0)
            t_osdTexts.push_back({x, y, text, color, pixelHeight, false, {0, 0, 0}, 0, true, 0, true});
    }

    void VideoFrameProcSophon::OSDDrawFilledRect(const util::Box& rect, const Color& color, uint8_t opacity) {
        if (t_osdFrame && rect.width > 0 && rect.height > 0 && opacity > 0)
            t_osdRects.push_back({rect, color, opacity});
    }

    void VideoFrameProcSophon::OSDDrawTextEx(int x, int y, const std::string& text, const Color& color,
                                             int fontSize, const Color& bgColor, uint8_t bgAlpha,
                                             bool outline, int bgPadding) {
        if (!t_osdFrame) {
            return;
        }
        if (x < 0 || y < 0 || x >= t_osdWidth || y >= t_osdHeight) {
            return;
        }
        t_osdTexts.push_back({x, y, text, color, fontSize, true, bgColor, bgAlpha, outline, bgPadding});
    }

    void VideoFrameProcSophon::EndOSD() {
        struct Cleanup {
            ~Cleanup() {
                t_osdLines.clear();
                t_osdTexts.clear();
                t_osdRects.clear();
                t_osdImage = nullptr;
                t_osdFrame = nullptr;
            }
        } cleanup;
        if (t_osdFrame && (!t_osdLines.empty() || !t_osdRects.empty() || !t_osdTexts.empty()))
            FlushOsdQueue();
    }

    void VideoFrameProcSophon::FlushOsdQueue() {
        auto& renderer = osd_service_;
        bool textReady = false;
        try {
            textReady = renderer.IsReady();
        } catch (const std::exception& error) {
            ReportOsdException("font readiness", error.what());
        } catch (...) {
            ReportOsdException("font readiness", "unknown exception");
        }
        const auto prepareNativeImage = [&]() {
            if (t_osdImage)
                return true;
            // Native fallback must describe the same tightly packed device
            // allocation. Never attach a default-stride image that expects
            // more bytes than the frame actually contains.
            const size_t ySize  = static_cast<size_t>(t_osdWidth) * t_osdHeight;
            const size_t uvSize = ySize / 4;
            const auto tryImage = [&](int* requestedStride) {
                auto image = CreateBMImage(t_osdWidth, t_osdHeight, PixelFormat::PIXEL_I420, requestedStride);
                if (!image)
                    return BmFrameImagePtr{};
                int strides[4] = {};
                int sizes[4]   = {};
                if (bm_image_get_stride(*image, strides) != BM_SUCCESS ||
                    bm_image_get_byte_size(*image, sizes) != BM_SUCCESS || strides[0] != t_osdWidth ||
                    strides[1] != t_osdWidth / 2 || strides[2] != t_osdWidth / 2 || sizes[0] < 0 ||
                    sizes[1] < 0 || sizes[2] < 0 || static_cast<size_t>(sizes[0]) != ySize ||
                    static_cast<size_t>(sizes[1]) != uvSize || static_cast<size_t>(sizes[2]) != uvSize ||
                    !VideoFrameAttach(t_osdFrame, image.get()))
                    return BmFrameImagePtr{};
                return image;
            };
            int requestedStride[3] = {t_osdWidth, t_osdWidth / 2, t_osdWidth / 2};
            auto image             = tryImage(requestedStride);
            if (!image) {
                LOG_WARN("{}", "OSD native fallback: explicit stride unavailable; checking default stride");
                image = tryImage(nullptr);
            }
            if (!image) {
                LOG_WARN("{}", "OSD native fallback: frame layout not supported safely");
                return false;
            }
            t_osdImage = std::move(image);
            return true;
        };
        const auto drawNativeText = [&] {
            if (t_osdTexts.empty() || !prepareNativeImage())
                return;
            for (auto& qt : t_osdTexts) {
                bmcv_point_t point{qt.x, qt.y};
                bmcv_color_t c{qt.color.red, qt.color.green, qt.color.blue};
                float fontScale = static_cast<float>(qt.fontSize) / 10.0f;
                const auto status =
                    bmcv_image_put_text(handle, *t_osdImage, qt.text.c_str(), point, c, fontScale, 0);
                if (status != BM_SUCCESS)
                    LOG_WARN("OSD native fallback text failed {}", status);
            }
        };
        const auto drawHardwareLines = [&] {
            // Preserve visible overlays if a device-host transfer fails.
            // This fallback is exceptional; normal rendering is CPU-based.
            if (t_osdLines.empty() || !prepareNativeImage())
                return;
            constexpr size_t kBatchSize = 128;
            bmcv_point_t start[kBatchSize];
            bmcv_point_t end[kBatchSize];
            for (const auto& group : t_osdLines) {
                size_t count = 0;
                const bmcv_color_t color{group.color.red, group.color.green, group.color.blue};
                const auto flush = [&] {
                    if (count > 0) {
                        const auto status = bmcv_image_draw_lines(
                            handle, *t_osdImage, start, end, static_cast<int>(count), color, group.width);
                        if (status != BM_SUCCESS)
                            LOG_WARN("OSD fallback draw lines failed {}", status);
                        count = 0;
                    }
                };
                for (const auto& line : group.lines) {
                    auto startPoint = line.first;
                    auto endPoint   = line.second;
                    if (!ClipNativeLine(startPoint, endPoint, t_osdWidth, t_osdHeight))
                        continue;
                    start[count] = {startPoint.x, startPoint.y};
                    end[count]   = {endPoint.x, endPoint.y};
                    if (++count == kBatchSize)
                        flush();
                }
                flush();
            }
        };
        const auto safeNativeText = [&]() noexcept {
            try {
                drawNativeText();
            } catch (const std::exception& error) {
                ReportOsdException("native text fallback", error.what());
            } catch (...) {
                ReportOsdException("native text fallback", "unknown exception");
            }
        };
        const auto safeNativeFallback = [&]() noexcept {
            try {
                drawHardwareLines();
            } catch (const std::exception& error) {
                ReportOsdException("native line fallback", error.what());
            } catch (...) {
                ReportOsdException("native line fallback", "unknown exception");
            }
            safeNativeText();
        };
        if (!textReady && t_osdLines.empty() && t_osdRects.empty()) {
            // Text-only with no TrueType renderer retains the original native fallback.
            safeNativeText();
            return;
        }

        // 1. Copy only the validated contiguous frame bytes. Unlike bm_image's
        // default strides this is the actual VideoFrame I420 allocation layout,
        // including valid even sizes such as 854x480.
        const size_t yPlaneSize  = static_cast<size_t>(t_osdWidth) * t_osdHeight;
        const size_t uvPlaneSize = yPlaneSize / 4;
        const auto frameSize     = static_cast<unsigned int>(t_osdFrame->GetSize());
        auto* deviceMemory       = reinterpret_cast<bm_mem_desc_t*>(t_osdFrame->GetData());
        if (!deviceMemory || bm_mem_get_device_size(*deviceMemory) < frameSize) {
            LOG_ERRO("{}", "OSD device allocation became invalid before rendering");
            return;
        }
        static thread_local std::vector<uint8_t> hostBuffer;
        bm_status_t ret = BM_SUCCESS;
        try {
            hostBuffer.resize(frameSize);
            auto* yBuf = hostBuffer.data();
            auto* uBuf = yBuf + yPlaneSize;
            auto* vBuf = uBuf + uvPlaneSize;

            // 2. Copy from the device memory descriptor to the host in one transfer.
            ret = bm_memcpy_d2s_partial(handle, hostBuffer.data(), *deviceMemory, frameSize);
            if (ret != BM_SUCCESS) {
                LOG_WARN("FlushOsdQueue - bm_memcpy_d2s_partial failed {}; falling back to native drawing",
                         ret);
                safeNativeFallback();
                return;
            }

            int uvStride = t_osdWidth / 2;

            // Lines are composed before text, matching existing preview/alarm draw order.
            RasterizeOsdLinesI420(yBuf, uBuf, vBuf, t_osdWidth, t_osdHeight, t_osdLines);
            for (const auto& rect : t_osdRects) {
                if (rect.outline)
                    RasterizeOsdRoundedRectOutlineI420(yBuf, uBuf, vBuf, t_osdWidth, t_osdHeight, rect.rect,
                                                       rect.color, rect.opacity, rect.radius);
                else
                    RasterizeOsdRoundedRectI420(yBuf, uBuf, vBuf, t_osdWidth, t_osdHeight, rect.rect,
                                                rect.color, rect.opacity, rect.radius);
            }

            // 3. Render each text entry using pre-baked outline + NEON accelerated blending
            for (auto& qt : t_osdTexts) {
                if (!textReady)
                    break;
                if (qt.fine) {
                    const auto bitmap =
                        renderer.RenderStringWithFineOutline(qt.text, static_cast<float>(qt.fontSize));
                    RasterizeOsdTextI420(yBuf, uBuf, vBuf, t_osdWidth, t_osdHeight, qt.x, qt.y, bitmap,
                                         qt.color);
                    continue;
                }
                float pixelHeight = static_cast<float>(qt.fontSize) * 1.5f;

                // RGB to YUV color conversion (text color)
                float rf = qt.color.red, gf = qt.color.green, bf = qt.color.blue;
                auto yc =
                    static_cast<uint8_t>(std::clamp(0.299f * rf + 0.587f * gf + 0.114f * bf, 0.f, 255.f));
                auto uc = static_cast<uint8_t>(
                    std::clamp(128.f - 0.169f * rf - 0.331f * gf + 0.500f * bf, 0.f, 255.f));
                auto vc = static_cast<uint8_t>(
                    std::clamp(128.f + 0.500f * rf - 0.419f * gf - 0.081f * bf, 0.f, 255.f));

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
                                BlendRowAlpha(yBuf + static_cast<size_t>(py * t_osdWidth + bgX0),
                                              bgAlphaRow.data(), bgYc, bgW);
                                if ((py & 1) == 0) {
                                    int uvY  = py / 2;
                                    int uvX0 = bgX0 / 2;
                                    int uvW  = (bgX1 / 2) - uvX0;
                                    if (uvW > 0) {
                                        bgAlphaRow.resize(static_cast<size_t>(uvW));
                                        std::fill(bgAlphaRow.begin(), bgAlphaRow.begin() + uvW, bgA);
                                        BlendRowAlpha(uBuf + static_cast<size_t>(uvY * uvStride + uvX0),
                                                      bgAlphaRow.data(), bgUc, uvW);
                                        BlendRowAlpha(vBuf + static_cast<size_t>(uvY * uvStride + uvX0),
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

                        BlendRowAlpha(yBuf + static_cast<size_t>(fy * t_osdWidth + fxStart),
                                      ob.outlineAlpha.data() + static_cast<size_t>(rowStart + bxStart), outYc,
                                      rowW);
                        BlendRowAlpha(yBuf + static_cast<size_t>(fy * t_osdWidth + fxStart),
                                      ob.bodyAlpha.data() + static_cast<size_t>(rowStart + bxStart), yc,
                                      rowW);

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
                                yBuf[yIdx] =
                                    static_cast<uint8_t>((bgA * bgYc + (255 - bgA) * yBuf[yIdx]) / 255);
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

                        BlendRowAlpha(yBuf + static_cast<size_t>(fy * t_osdWidth + fxStart),
                                      bitmap.alpha.data() + static_cast<size_t>(by * bitmap.width + bxStart),
                                      yc, rowW);
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

        } catch (const std::exception& error) {
            ReportOsdException("CPU composition", error.what());
            safeNativeFallback();
            return;
        } catch (...) {
            ReportOsdException("CPU composition", "unknown exception");
            safeNativeFallback();
            return;
        }

        // 4. Write back to device memory
        ret = bm_memcpy_s2d_partial(handle, *deviceMemory, hostBuffer.data(), frameSize);
        if (ret != BM_SUCCESS) {
            try {
                LOG_WARN("FlushOsdQueue - bm_memcpy_s2d_partial failed {}; falling back to native drawing",
                         ret);
            } catch (...) {
                // A failed log must not prevent the safe fallback attempt.
            }
            safeNativeFallback();
            return;
        }
        if (!textReady)
            safeNativeText();
    }

}  // namespace media
}  // namespace cosmo
