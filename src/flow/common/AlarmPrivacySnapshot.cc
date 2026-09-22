#include "flow/common/AlarmPrivacySnapshot.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>
#include <unordered_map>

#include "flow/common/AlgDataUnit.h"

namespace cosmo {
namespace {

    struct TaskSnapshot {
        std::shared_ptr<const AlarmPrivacySnapshot> snapshot;
        bool unavailable{false};
    };

    struct FrameSnapshots {
        std::weak_ptr<media::VideoFrame> frame;
        std::unordered_map<std::string, TaskSnapshot> tasks;
    };

    struct SnapshotCache {
        std::mutex mutex;
        std::unordered_map<const media::VideoFrame*, FrameSnapshots> frames;
        size_t publications{0};
    };

    SnapshotCache& Cache() {
        static SnapshotCache cache;
        return cache;
    }

    bool SameOwner(const std::weak_ptr<media::VideoFrame>& expected, const VideoFramePtr& frame) {
        return frame && !expected.owner_before(frame) && !frame.owner_before(expected) &&
               expected.lock().get() == frame.get();
    }

    bool Matches(const AlarmPrivacySnapshot& snapshot, const VideoFramePtr& frame) {
        return SameOwner(snapshot.frame, frame) && snapshot.frameName == frame->GetName() &&
               snapshot.streamIndex == frame->GetStreamIndex() &&
               snapshot.frameIndex == static_cast<int64_t>(frame->GetFrameIndex()) &&
               snapshot.timestamp == frame->GetTimestamp() &&
               snapshot.width == static_cast<int>(frame->GetWidth()) &&
               snapshot.height == static_cast<int>(frame->GetHeight());
    }

    bool Matches(const DataDetTrackClassify& result, const VideoFramePtr& frame) {
        return frame && result.streamIndex == frame->GetStreamIndex() &&
               result.frameIndex == static_cast<int64_t>(frame->GetFrameIndex()) &&
               result.timestamp == frame->GetTimestamp() && result.picWidth > 0 && result.picHeight > 0 &&
               result.picWidth == static_cast<int>(frame->GetWidth()) &&
               result.picHeight == static_cast<int>(frame->GetHeight());
    }

    bool IsPrefix(const std::vector<std::string>& prefix, const std::vector<std::string>& full) {
        return prefix.size() <= full.size() && std::equal(prefix.begin(), prefix.end(), full.begin());
    }

}  // namespace

void InvalidateAlarmPrivacySnapshot(AlgData& data) {
    data.alarmPrivacySnapshot.reset();
    data.alarmPrivacyUnavailable = true;
}

void CaptureAlarmPrivacySnapshot(AlgData& data, const std::string& detectorId,
                                 const std::vector<std::string>& knownLabels,
                                 const DataDetTrackClassify& result) {
    const auto& frame = data.chanDataDec.frame;
    if (data.alarmPrivacyUnavailable || detectorId.empty() || knownLabels.empty() ||
        !Matches(result, frame) || (data.chanDataDetect.detRet && !data.alarmPrivacySnapshot) ||
        (data.alarmPrivacySnapshot && !Matches(*data.alarmPrivacySnapshot, frame))) {
        InvalidateAlarmPrivacySnapshot(data);
        return;
    }

    auto snapshot         = data.alarmPrivacySnapshot
                                ? std::make_shared<AlarmPrivacySnapshot>(*data.alarmPrivacySnapshot)
                                : std::make_shared<AlarmPrivacySnapshot>();
    snapshot->frame       = frame;
    snapshot->frameName   = frame->GetName();
    snapshot->streamIndex = result.streamIndex;
    snapshot->frameIndex  = result.frameIndex;
    snapshot->timestamp   = result.timestamp;
    snapshot->width       = result.picWidth;
    snapshot->height      = result.picHeight;

    // Different runs of the same detector must not be mistaken for the same causal result.
    static std::atomic<uint64_t> nextSource{0};
    snapshot->sources.push_back(detectorId + ":" + std::to_string(++nextSource));
    for (const auto& label : knownLabels) {
        if (!label.empty() &&
            std::find(snapshot->labels.begin(), snapshot->labels.end(), label) == snapshot->labels.end()) {
            snapshot->labels.push_back(label);
        }
    }
    if (snapshot->labels.empty()) {
        InvalidateAlarmPrivacySnapshot(data);
        return;
    }
    for (const auto& target : result.targets) {
        // In particular, do not inspect bFilter, areaSign, alarm status, or track identity here.
        snapshot->targets.push_back({target.confidence.label, target.box});
    }
    data.alarmPrivacySnapshot = std::move(snapshot);
}

void PublishAlarmPrivacySnapshot(const AlgData& data, const std::string& taskId) {
    const auto& frame = data.chanDataDec.frame;
    const auto& task  = taskId.empty() ? data.taskId : taskId;
    if (!frame || task.empty()) {
        return;
    }
    auto& cache = Cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    // Reclaim expired metadata every 64 publications. Weak references never extend frame
    // lifetimes, while historical best frames keep their metadata available as long as needed.
    if (++cache.publications % 64 == 0) {
        for (auto it = cache.frames.begin(); it != cache.frames.end();) {
            it = it->second.frame.expired() ? cache.frames.erase(it) : std::next(it);
        }
    }
    auto& entry = cache.frames[frame.get()];
    if (!SameOwner(entry.frame, frame)) {
        entry.tasks.clear();
        entry.frame = frame;
    }
    auto& saved = entry.tasks[task];
    if (data.alarmPrivacyUnavailable || !data.alarmPrivacySnapshot ||
        !Matches(*data.alarmPrivacySnapshot, frame)) {
        saved.snapshot.reset();
        saved.unavailable = true;
        return;
    }
    if (saved.unavailable) {
        return;
    }
    if (!saved.snapshot) {
        saved.snapshot = data.alarmPrivacySnapshot;
        return;
    }
    const auto& incoming = data.alarmPrivacySnapshot;
    if (IsPrefix(saved.snapshot->sources, incoming->sources)) {
        saved.snapshot = incoming;
    } else if (!IsPrefix(incoming->sources, saved.snapshot->sources)) {
        // Independent/parallel detector branches cannot certify one complete result by
        // whichever finishes first. Fail closed instead of unioning partial branches.
        saved.snapshot.reset();
        saved.unavailable = true;
    }
}

std::shared_ptr<const AlarmPrivacySnapshot> FindAlarmPrivacySnapshot(const VideoFramePtr& frame,
                                                                     const std::string& taskId) {
    if (!frame || taskId.empty()) {
        return nullptr;
    }
    auto& cache = Cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto it = cache.frames.find(frame.get());
    if (it == cache.frames.end() || !SameOwner(it->second.frame, frame)) {
        return nullptr;
    }
    auto task = it->second.tasks.find(taskId);
    if (task == it->second.tasks.end() || task->second.unavailable || !task->second.snapshot ||
        !Matches(*task->second.snapshot, frame)) {
        return nullptr;
    }
    return task->second.snapshot;
}

bool HasAlarmPrivacyDetectors(const AlarmPrivacySnapshot& snapshot,
                              const std::vector<std::string>& expectedDetectors) {
    return std::all_of(expectedDetectors.begin(), expectedDetectors.end(), [&](const auto& detector) {
        return !detector.empty() &&
               std::any_of(snapshot.sources.begin(), snapshot.sources.end(), [&](const auto& source) {
                   return source.size() > detector.size() + 1 &&
                          source.compare(0, detector.size(), detector) == 0 && source[detector.size()] == ':';
               });
    });
}

}  // namespace cosmo
