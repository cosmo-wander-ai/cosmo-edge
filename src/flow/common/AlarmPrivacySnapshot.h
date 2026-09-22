#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/VideoFrame.h"
#include "util/Rect.h"

namespace cosmo {

struct AlgData;
struct DataDetTrackClassify;

struct AlarmPrivacyTarget {
    std::string label;
    util::Box box;
};

// Immutable detector output, captured before task thresholds, region flags, tracking, or
// business filters can narrow/mutate the target list. An empty targets vector is valid.
struct AlarmPrivacySnapshot {
    int64_t streamIndex{0};
    int64_t frameIndex{0};
    int64_t timestamp{0};
    int width{0};
    int height{0};
    std::vector<std::string> labels;
    std::vector<AlarmPrivacyTarget> targets;

    // Identity and causal provenance are required even if two images have identical timestamps.
    std::weak_ptr<media::VideoFrame> frame;
    std::string frameName;
    std::vector<std::string> sources;
};

// Call only after successful full-frame inference, BEFORE replacing chanDataDetect.
// knownLabels must contain only classes actually enabled for this detector invocation.
void CaptureAlarmPrivacySnapshot(AlgData& data, const std::string& detectorId,
                                 const std::vector<std::string>& knownLabels,
                                 const DataDetTrackClassify& result);

// Unsupported/partial detector stages cannot certify a complete privacy target set.
void InvalidateAlarmPrivacySnapshot(AlgData& data);

// Call on the task's incoming data before saving/using current or historical images.
// This also revokes an earlier partial result if the final task path has no valid metadata.
void PublishAlarmPrivacySnapshot(const AlgData& data, const std::string& taskId = {});

// The cache holds no strong image references. A retained best/base frame keeps its exact
// metadata addressable beyond the short detection-history window. Never uses nearest time.
std::shared_ptr<const AlarmPrivacySnapshot> FindAlarmPrivacySnapshot(const VideoFramePtr& frame,
                                                                     const std::string& taskId);

// Topology is supplied by the task lifecycle, so a detector branch that has not produced
// any result yet cannot be mistaken for a complete zero-target result.
bool HasAlarmPrivacyDetectors(const AlarmPrivacySnapshot& snapshot,
                              const std::vector<std::string>& expectedDetectors);

}  // namespace cosmo
