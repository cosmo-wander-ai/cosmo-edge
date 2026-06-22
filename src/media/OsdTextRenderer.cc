// OsdTextRenderer — Osd Text Renderer implementation.

#include "media/OsdTextRenderer.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "util/Log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

namespace cosmo {
namespace media {

    /// Glyph cache: caches rendered glyph bitmaps to avoid repeated stbtt_MakeCodepointBitmap calls
    struct GlyphKey {
        uint32_t codepoint;
        int pixelHeightX10;  // pixelHeight * 10 rounded, used as size key
        bool operator==(const GlyphKey& o) const {
            return codepoint == o.codepoint && pixelHeightX10 == o.pixelHeightX10;
        }
    };
    struct GlyphKeyHash {
        size_t operator()(const GlyphKey& k) const {
            return std::hash<uint64_t>()((static_cast<uint64_t>(k.codepoint) << 32) |
                                         static_cast<uint64_t>(k.pixelHeightX10));
        }
    };
    struct CachedGlyph {
        std::vector<uint8_t> bitmap;
        int width{0}, height{0};
        int ix0{0}, iy0{0};  // bitmap box offset
        int advance{0};
        int lsb{0};
    };

    // ── Outlined bitmap cache key ──
    struct OutlinedKey {
        std::string text;
        int pixelHeightX10;
        bool operator==(const OutlinedKey& o) const {
            return pixelHeightX10 == o.pixelHeightX10 && text == o.text;
        }
    };
    struct OutlinedKeyHash {
        size_t operator()(const OutlinedKey& k) const {
            size_t h = std::hash<std::string>{}(k.text);
            h ^= std::hash<int>{}(k.pixelHeightX10) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct OsdTextRenderer::Impl {
        stbtt_fontinfo fontInfo{};
        std::vector<uint8_t> fontBuffer;
        // glyph cache (Instance is global, mutex protects concurrent access)
        std::mutex cacheMutex;
        std::unordered_map<GlyphKey, CachedGlyph, GlyphKeyHash> glyphCache;

        // outlined bitmap cache (full string-level caching)
        std::mutex outlineCacheMutex;
        std::unordered_map<OutlinedKey, OsdTextRenderer::OutlinedTextBitmap, OutlinedKeyHash> outlineCache;
        static constexpr size_t kMaxOutlineCacheSize = 256;  // prevent unbounded growth

        const CachedGlyph& GetGlyph(uint32_t cp, float pixelHeight, float scale) {
            GlyphKey key{cp, static_cast<int>(pixelHeight * 10)};
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = glyphCache.find(key);
            if (it != glyphCache.end()) {
                return it->second;
            }
            // render and cache
            CachedGlyph g;
            stbtt_GetCodepointHMetrics(&fontInfo, static_cast<int>(cp), &g.advance, &g.lsb);
            int ix0, iy0, ix1, iy1;
            stbtt_GetCodepointBitmapBox(&fontInfo, static_cast<int>(cp), scale, scale, &ix0, &iy0, &ix1,
                                        &iy1);
            g.ix0    = ix0;
            g.iy0    = iy0;
            g.width  = ix1 - ix0;
            g.height = iy1 - iy0;
            if (g.width > 0 && g.height > 0) {
                g.bitmap.resize(static_cast<size_t>(g.width * g.height), 0);
                stbtt_MakeCodepointBitmap(&fontInfo, g.bitmap.data(), g.width, g.height, g.width, scale,
                                          scale, static_cast<int>(cp));
            }
            auto [insertIt, _] = glyphCache.emplace(key, std::move(g));
            return insertIt->second;
        }
    };

    OsdTextRenderer::OsdTextRenderer() = default;

    OsdTextRenderer::~OsdTextRenderer() = default;

    bool OsdTextRenderer::Init(const std::string& fontPath) {
        if (ready_) {
            return true;
        }

        std::ifstream ifs(fontPath, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            LOG_ERRO("OsdTextRenderer::Init - failed to open font: {}", fontPath);
            return false;
        }

        auto fileSize = ifs.tellg();
        if (fileSize <= 0) {
            LOG_ERRO("OsdTextRenderer::Init - empty font file: {}", fontPath);
            return false;
        }

        impl_ = std::make_unique<Impl>();
        impl_->fontBuffer.resize(static_cast<size_t>(fileSize));
        ifs.seekg(0);
        ifs.read(reinterpret_cast<char*>(impl_->fontBuffer.data()), fileSize);

        if (!stbtt_InitFont(&impl_->fontInfo, impl_->fontBuffer.data(), 0)) {
            LOG_ERRO("OsdTextRenderer::Init - stbtt_InitFont failed: {}", fontPath);
            impl_.reset();
            return false;
        }

        ready_ = true;
        LOG_INFO("OsdTextRenderer::Init - loaded font: {} ({} bytes)", fontPath, fileSize);
        return true;
    }

    // ── UTF-8 decoding ──────────────────────────────────────────
    static uint32_t DecodeUtf8(const char*& p, const char* end) {
        if (p >= end) {
            return 0;
        }
        auto c = static_cast<uint8_t>(*p);
        uint32_t cp;
        int extra;
        if (c < 0x80) {
            cp    = c;
            extra = 0;
        } else if ((c >> 5) == 0x6) {
            cp    = c & 0x1F;
            extra = 1;
        } else if ((c >> 4) == 0xE) {
            cp    = c & 0x0F;
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp    = c & 0x07;
            extra = 3;
        } else {
            ++p;
            return 0xFFFD;  // replacement char
        }
        ++p;
        for (int i = 0; i < extra && p < end; ++i, ++p) {
            cp = (cp << 6) | (static_cast<uint8_t>(*p) & 0x3F);
        }
        return cp;
    }

    OsdTextRenderer::TextBitmap OsdTextRenderer::RenderString(const std::string& utf8Text,
                                                              float pixelHeight) const {
        TextBitmap result;
        if (!ready_ || utf8Text.empty()) {
            return result;
        }

        const auto* font = &impl_->fontInfo;
        float scale      = stbtt_ScaleForPixelHeight(font, pixelHeight);

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);
        int scaledAscent  = static_cast<int>(std::ceil(static_cast<float>(ascent) * scale));
        int scaledDescent = static_cast<int>(std::floor(static_cast<float>(descent) * scale));
        int bitmapHeight  = scaledAscent - scaledDescent;

        // ── Pass 1: compute total width ──
        float totalWidth = 0;
        {
            const char* p   = utf8Text.c_str();
            const char* end = p + utf8Text.size();
            uint32_t prevCp = 0;
            while (p < end) {
                uint32_t cp = DecodeUtf8(p, end);
                if (cp == 0) {
                    break;
                }
                if (prevCp) {
                    totalWidth += scale * static_cast<float>(stbtt_GetCodepointKernAdvance(
                                              font, static_cast<int>(prevCp), static_cast<int>(cp)));
                }
                int advance, lsb;
                stbtt_GetCodepointHMetrics(font, static_cast<int>(cp), &advance, &lsb);
                totalWidth += static_cast<float>(advance) * scale;
                prevCp = cp;
            }
        }

        int bitmapWidth = static_cast<int>(std::ceil(totalWidth));
        if (bitmapWidth <= 0 || bitmapHeight <= 0) {
            return result;
        }

        result.width  = bitmapWidth;
        result.height = bitmapHeight;
        result.alpha.resize(static_cast<size_t>(bitmapWidth * bitmapHeight), 0);

        // ── Pass 2: render glyphs into bitmap (using glyph cache) ──
        float xPos = 0;
        {
            const char* p   = utf8Text.c_str();
            const char* end = p + utf8Text.size();
            uint32_t prevCp = 0;
            while (p < end) {
                uint32_t cp = DecodeUtf8(p, end);
                if (cp == 0) {
                    break;
                }
                if (prevCp) {
                    xPos += scale * static_cast<float>(stbtt_GetCodepointKernAdvance(
                                        font, static_cast<int>(prevCp), static_cast<int>(cp)));
                }

                // get rendered glyph from cache
                const auto& glyph = impl_->GetGlyph(cp, pixelHeight, scale);

                int drawX = static_cast<int>(std::round(xPos)) + glyph.ix0;
                int drawY = scaledAscent + glyph.iy0;

                if (glyph.width > 0 && glyph.height > 0) {
                    // blend into result bitmap
                    for (int gy = 0; gy < glyph.height; ++gy) {
                        int dy = drawY + gy;
                        if (dy < 0 || dy >= bitmapHeight) {
                            continue;
                        }
                        for (int gx = 0; gx < glyph.width; ++gx) {
                            int dx = drawX + gx;
                            if (dx < 0 || dx >= bitmapWidth) {
                                continue;
                            }
                            uint8_t src = glyph.bitmap[static_cast<size_t>(gy * glyph.width + gx)];
                            auto& dst   = result.alpha[static_cast<size_t>(dy * bitmapWidth + dx)];
                            dst         = std::max(dst, src);
                        }
                    }
                }
                xPos += static_cast<float>(glyph.advance) * scale;
                prevCp = cp;
            }
        }

        return result;
    }
    namespace {
        void DilateBody(const OsdTextRenderer::TextBitmap& inner, std::vector<uint8_t>& boldBody, int ow,
                        int /*oh*/, int totalRadius, int bodyBoldRadius) {
            int br2 = bodyBoldRadius * bodyBoldRadius;
            for (int y = 0; y < inner.height; ++y) {
                for (int x = 0; x < inner.width; ++x) {
                    uint8_t a = inner.alpha[static_cast<size_t>(y * inner.width + x)];
                    if (a == 0)
                        continue;
                    for (int dy = -bodyBoldRadius; dy <= bodyBoldRadius; ++dy) {
                        for (int dx = -bodyBoldRadius; dx <= bodyBoldRadius; ++dx) {
                            if (dx * dx + dy * dy > br2)
                                continue;
                            int bx  = x + totalRadius + dx;
                            int by2 = y + totalRadius + dy;
                            boldBody[static_cast<size_t>(by2 * ow + bx)] =
                                std::max(boldBody[static_cast<size_t>(by2 * ow + bx)], a);
                        }
                    }
                }
            }
        }

        void GenerateOutline(const OsdTextRenderer::TextBitmap& inner, std::vector<uint8_t>& outlineAlpha,
                             int ow, int /*oh*/, int totalRadius) {
            int r2 = totalRadius * totalRadius;
            for (int y = 0; y < inner.height; ++y) {
                for (int x = 0; x < inner.width; ++x) {
                    uint8_t a = inner.alpha[static_cast<size_t>(y * inner.width + x)];
                    if (a == 0)
                        continue;
                    for (int dy = -totalRadius; dy <= totalRadius; ++dy) {
                        for (int dx = -totalRadius; dx <= totalRadius; ++dx) {
                            if (dx == 0 && dy == 0)
                                continue;
                            if (dx * dx + dy * dy > r2)
                                continue;  // circular clip
                            int ox                                          = x + totalRadius + dx;
                            int oy                                          = y + totalRadius + dy;
                            outlineAlpha[static_cast<size_t>(oy * ow + ox)] = 255;
                        }
                    }
                }
            }
        }
    }  // namespace

    OsdTextRenderer::OutlinedTextBitmap OsdTextRenderer::RenderStringWithOutline(const std::string& utf8Text,
                                                                                 float pixelHeight) const {
        // ── lookup cache ──
        OutlinedKey cacheKey{utf8Text, static_cast<int>(pixelHeight * 10)};
        {
            std::lock_guard<std::mutex> lock(impl_->outlineCacheMutex);
            auto it = impl_->outlineCache.find(cacheKey);
            if (it != impl_->outlineCache.end()) {
                return it->second;  // cache hit, return directly (copies vector, but avoids re-rendering +
                                    // dilation)
            }
        }

        OutlinedTextBitmap result;

        // render normal text first
        auto inner = RenderString(utf8Text, pixelHeight);
        if (inner.alpha.empty()) {
            return result;
        }

        // expand bitmap dimensions: body bold + outline width
        static constexpr int kBodyBoldRadius = 1;  // body stroke thickening
        static constexpr int kOutlineWidth   = 1;  // visible dark outline around bold body
        static constexpr int kTotalRadius    = kBodyBoldRadius + kOutlineWidth;  // total expansion
        int ow                               = inner.width + 2 * kTotalRadius;
        int oh                               = inner.height + 2 * kTotalRadius;
        result.width                         = ow;
        result.height                        = oh;
        result.offsetX                       = kTotalRadius;
        result.offsetY                       = kTotalRadius;
        result.outlineAlpha.resize(static_cast<size_t>(ow * oh), 0);
        result.bodyAlpha.resize(static_cast<size_t>(ow * oh), 0);

        // copy body text to bodyAlpha (offset by radius)
        for (int y = 0; y < inner.height; ++y) {
            for (int x = 0; x < inner.width; ++x) {
                uint8_t a = inner.alpha[static_cast<size_t>(y * inner.width + x)];
                if (a > 0) {
                    result.bodyAlpha[static_cast<size_t>((y + kTotalRadius) * ow + (x + kTotalRadius))] = a;
                }
            }
        }

        // Dilate body to make text strokes bolder (soft: propagate AA alpha for subtle thickening)
        std::vector<uint8_t> boldBody(static_cast<size_t>(ow * oh), 0);
        DilateBody(inner, boldBody, ow, oh, kTotalRadius, kBodyBoldRadius);

        // Merge: keep original AA where present, fill expanded area with 255
        for (size_t i = 0; i < static_cast<size_t>(ow * oh); ++i) {
            result.bodyAlpha[i] = std::max(result.bodyAlpha[i], boldBody[i]);
        }

        // dilate to generate outline alpha: covers body + outline width
        // Use binary alpha (255) to avoid blurry halo from anti-aliased edge propagation
        GenerateOutline(inner, result.outlineAlpha, ow, oh, kTotalRadius);

        // Subtract body from outline: prevent dark outline from bleeding under
        // anti-aliased text edges (which would darken them and make text look thinner)
        for (size_t i = 0; i < static_cast<size_t>(ow * oh); ++i) {
            int eff = static_cast<int>(result.outlineAlpha[i]) - static_cast<int>(result.bodyAlpha[i]);
            result.outlineAlpha[i] = static_cast<uint8_t>(std::max(0, eff));
        }

        // ── write to cache ──
        {
            std::lock_guard<std::mutex> lock(impl_->outlineCacheMutex);
            if (impl_->outlineCache.size() >= Impl::kMaxOutlineCacheSize) {
                impl_->outlineCache.clear();  // simple LRU: clear when full
            }
            impl_->outlineCache.emplace(cacheKey, result);
        }

        return result;
    }

}  // namespace media
}  // namespace cosmo
