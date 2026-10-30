#pragma once

#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

#include "media/VideoFrame.h"
#include "util/Rect.h"

namespace cosmo {
struct AiTargetAnnotation;
struct ModelInfo;
namespace media {
    class IOsdTextRenderer;
}
namespace service {
    class IVideoFrameOSD;
}
namespace alarm {

    // Display-only translation. Runtime class identifiers are never modified.
    std::string ResolveAlarmTargetLabel(const AiTargetAnnotation& annotation, const ModelInfo& model,
                                        const std::string& presetRoot, const nlohmann::json& translations);
    std::string FormatAlarmTargetLabel(const std::string& label, std::optional<float> score);

    struct AlarmImageTargetText {
        util::Box box;
        std::vector<std::string> labels;
    };

    struct AlarmImageText {
        std::string text;
        int x{0};
        int y{0};
        int fontSize{0};
    };

    // Sophon's session renderer currently scales the public font size by 1.5.
    float AlarmImageTextRenderScale();
    std::vector<AlarmImageText> BuildAlarmImageTextLayout(const std::vector<AlarmImageTargetText>& targets,
                                                          int width, int height, bool showSize,
                                                          const media::IOsdTextRenderer& renderer,
                                                          float renderScale, const std::string& title = {});

    // Uses a complete service-owned session; commits before the caller encodes JPEG.
    bool DrawAlarmImageText(service::IVideoFrameOSD& osd, const VideoFramePtr& frame,
                            const std::vector<AlarmImageText>& text);

}  // namespace alarm
}  // namespace cosmo
