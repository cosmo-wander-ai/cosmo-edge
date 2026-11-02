#include "flow/alarm/AlarmImageAnnotations.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>

#include "media/Color.h"
#include "media/IOsdTextRenderer.h"
#include "nlohmann/json.hpp"
#include "service/media/IVideoFrameOSD.h"
#include "service/model/dto/ModelInfo.h"
#include "util/AiTypes.h"
#include "util/PathUtil.h"

namespace cosmo::alarm {
namespace {
    constexpr int kPadding = 3;
    constexpr int kMargin  = kPadding + 2;  // Includes the renderer's one-pixel outline.
    constexpr int kLineGap = 0;

    std::string ResourceToken(const std::string& value) {
        std::string result;
        for (const unsigned char c : value) {
            if (c >= 'A' && c <= 'Z')
                result.push_back(static_cast<char>(c - 'A' + 'a'));
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
                result.push_back(static_cast<char>(c));
            else
                return {};  // No guessed normalization of arbitrary customer class names.
        }
        return result;
    }

    // Keep text bounded and on one line, and never split a UTF-8 codepoint.
    std::string DisplayText(const std::string& value, size_t maxBytes = 256) {
        std::string result;
        for (size_t i = 0; i < value.size() && result.size() < maxBytes;) {
            const auto c = static_cast<unsigned char>(value[i]);
            if (c < 0x20 || c == 0x7f) {
                ++i;
                continue;
            }
            const size_t bytes = c < 0x80
                                     ? 1
                                     : (c >= 0xc2 && c < 0xe0
                                            ? 2
                                            : (c < 0xf0 && c >= 0xe0 ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 0)));
            bool valid         = bytes != 0 && i + bytes <= value.size();
            for (size_t j = 1; valid && j < bytes; ++j)
                valid = (static_cast<unsigned char>(value[i + j]) & 0xc0) == 0x80;
            if (!valid) {
                result.push_back('?');
                ++i;
            } else {
                if (result.size() + bytes > maxBytes)
                    break;
                result.append(value, i, bytes);
                i += bytes;
            }
        }
        return result;
    }

    void RemoveLastCodepoint(std::string& text) {
        if (text.empty())
            return;
        size_t start = text.size() - 1;
        while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80)
            --start;
        text.resize(start);
    }

    struct FittedText {
        std::string text;
        int width{0};
        int height{0};
        int fontSize{0};
    };

    FittedText FitText(const std::string& input, int fontSize, int maxWidth,
                       const media::IOsdTextRenderer& renderer, float scale) {
        FittedText result;
        result.text     = DisplayText(input);
        result.fontSize = fontSize;
        if (result.text.empty() || maxWidth <= 0)
            return {};
        const auto measure = [&](const std::string& text) {
            return renderer.RenderString(text, static_cast<float>(fontSize) * scale);
        };
        auto bitmap = measure(result.text);
        if (bitmap.width > maxWidth) {
            // Preserve the measured score of a single class even when its name is long.
            std::string suffix;
            const auto space = result.text.rfind(' ');
            if (result.text.back() == '%' && space != std::string::npos &&
                result.text.find(" / ") == std::string::npos) {
                suffix = result.text.substr(space);
                result.text.resize(space);
            }
            while (!result.text.empty()) {
                RemoveLastCodepoint(result.text);
                const auto candidate = result.text + "..." + suffix;
                bitmap               = measure(candidate);
                if (bitmap.width <= maxWidth) {
                    result.text = candidate;
                    break;
                }
            }
            if (result.text.empty()) {
                // Very narrow images may fit only the measured score. Never drop it
                // in favour of a partial class name with no confidence.
                if (suffix.empty())
                    return {};
                result.text = suffix.substr(1);
                bitmap      = measure(result.text);
                if (bitmap.width > maxWidth)
                    return {};
            }
        }
        if (bitmap.alpha.empty() || bitmap.width <= 0 || bitmap.height <= 0)
            return {};
        result.width  = bitmap.width;
        result.height = bitmap.height;
        return result;
    }
}  // namespace

std::string ResolveAlarmTargetLabel(const AiTargetAnnotation& annotation, const ModelInfo& model,
                                    const std::string& presetRoot, const nlohmann::json& translations) {
    const auto& raw = annotation.label;
    if (annotation.atomic_code.empty() || model.id != annotation.atomic_code ||
        !cosmo::path::IsWithinRoot(presetRoot, model.path) || !translations.is_object())
        return raw;
    const auto token = ResourceToken(raw);
    if (token.empty())
        return raw;
    for (const auto& label : model.labels) {
        if (label.code != raw || label.labelName != raw || label.label.empty())
            continue;
        const auto key = "resource.label." + model.id + "." + token + "." + model.id + "." + label.label +
                         "." + token + ".namecn";
        const auto found = translations.find(key);
        if (found != translations.end() && found->is_string() &&
            !found->get_ref<const std::string&>().empty())
            return found->get<std::string>();
    }
    return raw;
}

std::string FormatAlarmTargetLabel(const std::string& label, std::optional<float> score) {
    // Reserve room for the score before the layout helper applies its total text bound.
    auto text = DisplayText(label, 192);
    if (text.empty())
        return {};
    if (score && std::isfinite(*score) && *score >= 0.0F && *score <= 1.0F) {
        std::ostringstream formatted;
        formatted.imbue(std::locale::classic());
        formatted << text << ' ' << std::fixed << std::setprecision(1) << (*score * 100.0F) << '%';
        return formatted.str();
    }
    return text;
}

float AlarmImageTextRenderScale() {
#ifdef COSMO_MEDIA_USE_SOPHON_BACKEND
    return 1.5F;
#else
    return 1.0F;
#endif
}

int AlarmImageTopSafeMargin(int height) {
    if (height <= 0)
        return 0;
    // Most camera timestamps are at the top edge, but their location and
    // dimensions are not supplied to this renderer. Limit the heuristic on
    // small images and never claim that it detects or avoids every camera OSD.
    return std::min(std::clamp(height / 25, 16, 64), height / 4);
}

std::vector<AlarmImageText> BuildAlarmImageTextLayout(const std::vector<AlarmImageTargetText>& targets,
                                                      int width, int height, bool showSize,
                                                      const media::IOsdTextRenderer& renderer,
                                                      float renderScale, const std::string& title) {
    std::vector<AlarmImageText> result;
    if (width < 32 || height < 32 || !renderer.IsReady() || !std::isfinite(renderScale) || renderScale <= 0)
        return result;
    const int fontSize = std::max(1, static_cast<int>(std::clamp(height / 45, 18, 28) / renderScale));
    const int sizeFont = std::max(1, static_cast<int>(std::clamp(height / 54, 14, 24) / renderScale));
    const int maxWidth = width - 2 * kMargin;
    int minY           = kMargin + AlarmImageTopSafeMargin(height);
    bool headingDrawn  = false;
    const auto heading = FitText(title, fontSize, maxWidth, renderer, renderScale);
    if (!heading.text.empty() && heading.height + 2 * kMargin <= height) {
        // On short images keep the title in-bounds even if that leaves less
        // than the preferred top strip. Preserve the existing title setting.
        const int headingY = std::min(minY, height - heading.height - kMargin);
        result.push_back({heading.text, kMargin, headingY, heading.fontSize});
        headingDrawn = true;
        minY         = headingY + heading.height + kMargin + kLineGap;
    }
    for (const auto& target : targets) {
        if (target.box.width <= 0 || target.box.height <= 0)
            continue;
        std::string labels;
        for (const auto& label : target.labels) {
            if (label.empty())
                continue;
            if (!labels.empty())
                labels += " / ";
            labels += label;
            if (labels.size() >= 256)
                break;
        }
        std::vector<FittedText> lines;
        auto category = FitText(labels, fontSize, maxWidth, renderer, renderScale);
        if (!category.text.empty())
            lines.push_back(std::move(category));
        if (showSize) {
            auto size = FitText(std::to_string(target.box.width) + "x" + std::to_string(target.box.height),
                                sizeFont, maxWidth, renderer, renderScale);
            if (!size.text.empty())
                lines.push_back(std::move(size));
        }
        if (lines.empty())
            continue;
        int blockHeight = 0;
        int blockWidth  = 0;
        for (const auto& line : lines) {
            blockHeight += line.height + 2 * kPadding + kLineGap;
            blockWidth = std::max(blockWidth, line.width);
        }
        int labelMinY = minY;
        if (blockHeight + labelMinY + kMargin > height && !headingDrawn) {
            // On very short images, do not lose the only target annotation
            // solely to reserve a heuristic camera OSD strip.
            labelMinY = kMargin;
        }
        if (blockHeight + labelMinY + kMargin > height)
            continue;
        const int x       = std::clamp(target.box.x, kMargin, width - blockWidth - kMargin);
        const auto boxTop = static_cast<int64_t>(target.box.y);
        int64_t y         = boxTop - blockHeight - kPadding;
        if (y < labelMinY)
            y = boxTop + kPadding + 1;  // Near the image top, use the inside edge.
        y = std::clamp<int64_t>(y, labelMinY, height - blockHeight - kMargin);
        for (const auto& line : lines) {
            result.push_back({line.text, x, static_cast<int>(y), line.fontSize});
            y += line.height + 2 * kPadding + kLineGap;
        }
    }
    return result;
}

void QueueAlarmImageText(service::IVideoFrameOSD& osd, const std::vector<AlarmImageText>& text) {
    for (const auto& line : text)
        osd.OSDDrawTextEx(line.x, line.y, line.text, {255, 255, 255}, line.fontSize, {5, 8, 22}, 199, true,
                          kPadding);
}

bool DrawAlarmImageText(service::IVideoFrameOSD& osd, const VideoFramePtr& frame,
                        const std::vector<AlarmImageText>& text) {
    if (text.empty())
        return true;
    if (!osd.BeginOSD(frame))
        return false;
    struct SessionGuard {
        service::IVideoFrameOSD& osd;
        bool active{true};
        ~SessionGuard() {
            if (active) {
                try {
                    osd.EndOSD();
                } catch (...) {
                    // Service EndOSD releases its session lock even if rendering fails.
                }
            }
        }
        void Finish() {
            active = false;
            osd.EndOSD();
        }
    } session{osd};
    QueueAlarmImageText(osd, text);
    session.Finish();
    return true;
}
}  // namespace cosmo::alarm
