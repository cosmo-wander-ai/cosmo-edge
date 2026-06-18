#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/IOsdTextRenderer.h"

namespace cosmo {
namespace media {

    /// TrueType text renderer (stb_truetype wrapper).
    /// Thread safety: RenderString() is safe to call concurrently after Init().
    ///
    /// Implements IOsdTextRenderer and is registered in ServiceRegistry.
    /// Do NOT use the old static Instance() — access via ServiceRegistry::Get<IOsdTextRenderer>().
    class OsdTextRenderer : public IOsdTextRenderer {
    public:
        OsdTextRenderer();
        ~OsdTextRenderer() override;

        OsdTextRenderer(const OsdTextRenderer&)            = delete;
        OsdTextRenderer& operator=(const OsdTextRenderer&) = delete;

        /// Load a TrueType/OpenType font file
        bool Init(const std::string& fontPath) override;
        bool IsReady() const override {
            return ready_;
        }

        /// Render a UTF-8 string to an alpha bitmap
        /// @param pixelHeight target character height (pixels)
        TextBitmap RenderString(const std::string& utf8Text, float pixelHeight) const override;

        /// Render text with 1px outline, pre-baked into unified bitmap
        /// Avoids 4-pass traversal in FlushTextQueue
        OutlinedTextBitmap RenderStringWithOutline(const std::string& utf8Text,
                                                   float pixelHeight) const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        bool ready_{false};
    };

}  // namespace media
}  // namespace cosmo
