#include "flow/alarm/AlarmImagePrivacy.h"

#include <exception>

#include "flow/common/AlarmPrivacySnapshot.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/Log.h"

namespace cosmo {

VideoFramePtr PrepareAlarmImage(const VideoFramePtr& frame, const std::string& taskId,
                                const util::AlarmImagePrivacy& privacy,
                                const std::vector<std::string>& expectedDetectors) {
    if (!VideoFrameValid(frame)) {
        return nullptr;
    }
    try {
        auto& registry = service::ServiceRegistry::Instance();
        if (!privacy.Requested()) {
            return registry.Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(frame);
        }
        if (!privacy.Valid()) {
            LOG_WARN("[{}] Omit alarm image: invalid privacy policy", taskId);
            return nullptr;
        }
        auto snapshot = FindAlarmPrivacySnapshot(frame, taskId);
        if (!snapshot) {
            LOG_WARN("[{}] Omit alarm image: complete same-frame detections unavailable", taskId);
            return nullptr;
        }
        if (!HasAlarmPrivacyDetectors(*snapshot, expectedDetectors)) {
            LOG_WARN("[{}] Omit alarm image: a required detector has no complete same-frame result", taskId);
            return nullptr;
        }
        if (privacy.labels != "*") {
            // A label with zero detections is safe only if a detector for that label actually ran.
            size_t begin = 0;
            do {
                const auto end = privacy.labels.find(',', begin);
                const auto label =
                    privacy.labels.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                if (std::find(snapshot->labels.begin(), snapshot->labels.end(), label) ==
                    snapshot->labels.end()) {
                    LOG_WARN("[{}] Omit alarm image: selected privacy class unavailable", taskId);
                    return nullptr;
                }
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            } while (begin < privacy.labels.size());
        }
        std::vector<util::Box> boxes;
        for (const auto& target : snapshot->targets) {
            if (privacy.Selects(target.label)) {
                boxes.push_back(target.box);
            }
        }
        auto masked =
            registry.Get<service::IVideoFrameTransform>().MosaicCopy(frame, boxes, privacy.Strength());
        if (!VideoFrameValid(masked) || masked == frame) {
            LOG_WARN("[{}] Omit alarm image: mosaic copy failed", taskId);
            return nullptr;
        }
        // Existing OSD/crop paths expect the JPEG-compatible format. Any fallback here is
        // still a private, already-masked frame; the original never reaches image encoders.
        auto prepared = registry.Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(masked);
        return VideoFrameValid(prepared) && prepared != frame ? prepared : nullptr;
    } catch (const std::exception& error) {
        LOG_WARN("[{}] Omit alarm image: processing failed ({})", taskId, error.what());
    } catch (...) {
        LOG_WARN("[{}] Omit alarm image: processing failed", taskId);
    }
    return nullptr;
}

}  // namespace cosmo
