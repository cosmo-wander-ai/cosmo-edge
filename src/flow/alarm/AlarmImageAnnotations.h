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
    // Reserve a modest strip at the top for common camera-supplied text. This
    // is a layout heuristic, not detection of the camera's actual OSD position.
    int AlarmImageTopSafeMargin(int height);
    std::vector<AlarmImageText> BuildAlarmImageTextLayout(const std::vector<AlarmImageTargetText>& targets,
                                                          int width, int height, bool showSize,
                                                          const media::IOsdTextRenderer& renderer,
                                                          float renderScale, const std::string& title = {});

    // Queue styled labels in an existing service-owned session. The caller
    // must have successfully entered BeginOSD and must finish with EndOSD.
    void QueueAlarmImageText(service::IVideoFrameOSD& osd, const std::vector<AlarmImageText>& text);

    // Uses a complete service-owned session; commits before the caller encodes JPEG.
    bool DrawAlarmImageText(service::IVideoFrameOSD& osd, const VideoFramePtr& frame,
                            const std::vector<AlarmImageText>& text);

}  // namespace alarm
}  // namespace cosmo
