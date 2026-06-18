// OSD TrueType text rendering interface.
// Provides font initialization and text-to-bitmap rendering for video overlays.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cosmo::media {

class IOsdTextRenderer {
public:
    virtual ~IOsdTextRenderer() = default;

    /// Load a TrueType/OpenType font file.
    virtual bool Init(const std::string& fontPath) = 0;

    /// Returns true if a font has been successfully loaded.
    virtual bool IsReady() const = 0;

    /// Render result: grayscale bitmap with alpha channel.
    struct TextBitmap {
        std::vector<uint8_t> alpha;  // alpha values, row-major
        int width{0};
        int height{0};
    };

    /// Render a UTF-8 string to an alpha bitmap.
    /// @param pixelHeight target character height (pixels)
    virtual TextBitmap RenderString(const std::string& utf8Text, float pixelHeight) const = 0;

    /// Render result with pre-baked outline: two alpha channels sharing the same dimensions.
    struct OutlinedTextBitmap {
        std::vector<uint8_t> outlineAlpha;  // outline alpha (includes dilated text region)
        std::vector<uint8_t> bodyAlpha;     // body text alpha
        int width{0};
        int height{0};
        int offsetX{1};  // offset relative to original position (dilation expansion)
        int offsetY{1};
    };

    /// Render text with 1px outline, pre-baked into unified bitmap.
    /// Avoids 4-pass traversal in FlushTextQueue.
    virtual OutlinedTextBitmap RenderStringWithOutline(const std::string& utf8Text,
                                                       float pixelHeight) const = 0;
};

}  // namespace cosmo::media
