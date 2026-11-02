#include "media/PreviewPipelineMetrics.h"

#include <algorithm>
#include <limits>

namespace cosmo::media {
namespace {
    constexpr auto kTimedWindow       = std::chrono::seconds(5);
    constexpr size_t kMaxTimedSamples = 512;
    void DecrementWithoutUnderflow(std::atomic<uint64_t>& value) {
        auto current = value.load(std::memory_order_relaxed);
        while (current != 0 &&
               !value.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
        }
    }

    void UpdateMaximum(std::atomic<uint64_t>& value, uint64_t sample) {
        auto current = value.load(std::memory_order_relaxed);
        while (sample > current && !value.compare_exchange_weak(current, sample, std::memory_order_relaxed)) {
        }
    }
}  // namespace

PreviewPipelineMetrics::PreviewToken PreviewPipelineMetrics::BeginTimedPreview(
    const std::string& channel_id, const std::string& algorithm_id) {
    if (channel_id.empty() || algorithm_id.empty())
        return 0;
    std::lock_guard<std::mutex> lock(timed_mutex_);
    const auto key      = std::make_pair(channel_id, algorithm_id);
    const auto previous = timed_keys_.find(key);
    if (previous != timed_keys_.end())
        timed_previews_.erase(previous->second);
    do {
        ++next_timed_token_;
    } while (next_timed_token_ == 0 || timed_previews_.count(next_timed_token_) != 0);
    timed_keys_[key] = next_timed_token_;
    timed_previews_.emplace(next_timed_token_, TimedPreviewState{key, {}, {}, false});
    return next_timed_token_;
}

void PreviewPipelineMetrics::MarkTimedPreviewReady(PreviewToken token) {
    std::lock_guard<std::mutex> lock(timed_mutex_);
    const auto it = timed_previews_.find(token);
    if (it != timed_previews_.end())
        it->second.ready = true;
}

void PreviewPipelineMetrics::EndTimedPreview(PreviewToken token) {
    if (token == 0)
        return;
    std::lock_guard<std::mutex> lock(timed_mutex_);
    auto it = timed_previews_.find(token);
    if (it == timed_previews_.end())
        return;
    const auto key = timed_keys_.find(it->second.key);
    if (key != timed_keys_.end() && key->second == token)
        timed_keys_.erase(key);
    timed_previews_.erase(it);
}

void PreviewPipelineMetrics::RecordTimedSample(PreviewToken token, uint64_t nanoseconds, SampleTime at,
                                               bool osd) {
    if (token == 0)
        return;
    std::lock_guard<std::mutex> lock(timed_mutex_);
    const auto it = timed_previews_.find(token);
    if (it == timed_previews_.end())
        return;
    auto& samples = osd ? it->second.osd : it->second.encode;
    while (!samples.empty() && at >= samples.front().at && at - samples.front().at >= kTimedWindow)
        samples.pop_front();
    if (samples.size() >= kMaxTimedSamples)
        samples.pop_front();
    samples.push_back({at, nanoseconds});
}

void PreviewPipelineMetrics::RecordTimedOsd(PreviewToken token, uint64_t nanoseconds, SampleTime at) {
    RecordTimedSample(token, nanoseconds, at, true);
}

void PreviewPipelineMetrics::RecordTimedEncode(PreviewToken token, uint64_t nanoseconds, SampleTime at) {
    RecordTimedSample(token, nanoseconds, at, false);
}

PreviewDurationAggregate PreviewPipelineMetrics::AggregateTimedSamples(const std::deque<TimedSample>& samples,
                                                                       SampleTime now) {
    PreviewDurationAggregate result;
    for (const auto& sample : samples) {
        if (sample.at > now || now - sample.at >= kTimedWindow)
            continue;
        if (result.count == 0)
            result.min_nanoseconds = sample.nanoseconds;
        else
            result.min_nanoseconds = std::min(result.min_nanoseconds, sample.nanoseconds);
        result.max_nanoseconds = std::max(result.max_nanoseconds, sample.nanoseconds);
        result.total_nanoseconds +=
            std::min(sample.nanoseconds, std::numeric_limits<uint64_t>::max() - result.total_nanoseconds);
        ++result.count;
    }
    return result;
}

PreviewDurationSnapshot PreviewPipelineMetrics::TimedPreview(const std::string& channel_id,
                                                             const std::string& algorithm_id,
                                                             SampleTime now) const {
    std::lock_guard<std::mutex> lock(timed_mutex_);
    const auto key = timed_keys_.find({channel_id, algorithm_id});
    if (key == timed_keys_.end())
        return {};
    const auto it = timed_previews_.find(key->second);
    if (it == timed_previews_.end() || !it->second.ready)
        return {};
    return {AggregateTimedSamples(it->second.osd, now), AggregateTimedSamples(it->second.encode, now)};
}

void PreviewPipelineMetrics::PublisherOpened() {
    active_publishers_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::PublisherClosed() {
    DecrementWithoutUnderflow(active_publishers_);
}

void PreviewPipelineMetrics::PreviewStarted(bool algorithm_preview, uint64_t first_frame_nanoseconds) {
    active_preview_streams_.fetch_add(1, std::memory_order_relaxed);
    auto& active_by_type =
        algorithm_preview ? active_algorithm_preview_streams_ : active_raw_preview_streams_;
    active_by_type.fetch_add(1, std::memory_order_relaxed);
    preview_stream_starts_.fetch_add(1, std::memory_order_relaxed);
    first_frames_.fetch_add(1, std::memory_order_relaxed);
    first_frame_nanoseconds_.fetch_add(first_frame_nanoseconds, std::memory_order_relaxed);
    UpdateMaximum(first_frame_max_nanoseconds_, first_frame_nanoseconds);
}

void PreviewPipelineMetrics::PreviewStopped(bool algorithm_preview) {
    DecrementWithoutUnderflow(active_preview_streams_);
    auto& active_by_type =
        algorithm_preview ? active_algorithm_preview_streams_ : active_raw_preview_streams_;
    DecrementWithoutUnderflow(active_by_type);
    preview_stream_stops_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::PreviewFailed() {
    preview_stream_failures_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordOsdFrame(uint64_t nanoseconds) {
    osd_frames_.fetch_add(1, std::memory_order_relaxed);
    osd_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordPublishedFrame(uint64_t nanoseconds) {
    published_frames_.fetch_add(1, std::memory_order_relaxed);
    publish_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordRgaOperation(bool success, uint64_t nanoseconds) {
    if (success) {
        rga_frames_.fetch_add(1, std::memory_order_relaxed);
        rga_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        rga_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppEncode(bool success, uint64_t nanoseconds) {
    if (success) {
        mpp_encoded_frames_.fetch_add(1, std::memory_order_relaxed);
        mpp_encode_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        mpp_encode_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppDecode(bool success, uint64_t nanoseconds) {
    if (success) {
        mpp_decoded_frames_.fetch_add(1, std::memory_order_relaxed);
        mpp_decode_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        mpp_decode_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppDecodeFallback() {
    mpp_decode_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordMppCopyOut(bool success, uint64_t nanoseconds) {
    if (success) {
        mpp_copy_out_frames_.fetch_add(1, std::memory_order_relaxed);
        mpp_copy_out_nanoseconds_.fetch_add(nanoseconds, std::memory_order_relaxed);
    } else {
        mpp_copy_out_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void PreviewPipelineMetrics::RecordMppRgaCopyOut(bool success) {
    auto& counter = success ? mpp_rga_copy_out_frames_ : mpp_rga_copy_out_failures_;
    counter.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordMppCpuCopyOutFallback() {
    mpp_cpu_copy_out_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordMppRgaCopyIn(bool success) {
    auto& counter = success ? mpp_rga_copy_in_frames_ : mpp_rga_copy_in_failures_;
    counter.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordMppCpuCopyInFallback() {
    mpp_cpu_copy_in_fallbacks_.fetch_add(1, std::memory_order_relaxed);
}

void PreviewPipelineMetrics::RecordMppEarlyDrop() {
    mpp_early_dropped_frames_.fetch_add(1, std::memory_order_relaxed);
}

PreviewPipelineMetricsSnapshot PreviewPipelineMetrics::Snapshot() const {
    return {
        active_publishers_.load(std::memory_order_relaxed),
        active_preview_streams_.load(std::memory_order_relaxed),
        active_raw_preview_streams_.load(std::memory_order_relaxed),
        active_algorithm_preview_streams_.load(std::memory_order_relaxed),
        preview_stream_starts_.load(std::memory_order_relaxed),
        preview_stream_stops_.load(std::memory_order_relaxed),
        preview_stream_failures_.load(std::memory_order_relaxed),
        osd_frames_.load(std::memory_order_relaxed),
        osd_nanoseconds_.load(std::memory_order_relaxed),
        published_frames_.load(std::memory_order_relaxed),
        publish_nanoseconds_.load(std::memory_order_relaxed),
        first_frames_.load(std::memory_order_relaxed),
        first_frame_nanoseconds_.load(std::memory_order_relaxed),
        first_frame_max_nanoseconds_.load(std::memory_order_relaxed),
        rga_frames_.load(std::memory_order_relaxed),
        rga_nanoseconds_.load(std::memory_order_relaxed),
        rga_failures_.load(std::memory_order_relaxed),
        mpp_encoded_frames_.load(std::memory_order_relaxed),
        mpp_encode_nanoseconds_.load(std::memory_order_relaxed),
        mpp_encode_failures_.load(std::memory_order_relaxed),
        mpp_decoded_frames_.load(std::memory_order_relaxed),
        mpp_decode_nanoseconds_.load(std::memory_order_relaxed),
        mpp_decode_failures_.load(std::memory_order_relaxed),
        mpp_decode_fallbacks_.load(std::memory_order_relaxed),
        mpp_copy_out_frames_.load(std::memory_order_relaxed),
        mpp_copy_out_nanoseconds_.load(std::memory_order_relaxed),
        mpp_copy_out_failures_.load(std::memory_order_relaxed),
        mpp_rga_copy_out_frames_.load(std::memory_order_relaxed),
        mpp_rga_copy_out_failures_.load(std::memory_order_relaxed),
        mpp_cpu_copy_out_fallbacks_.load(std::memory_order_relaxed),
        mpp_rga_copy_in_frames_.load(std::memory_order_relaxed),
        mpp_rga_copy_in_failures_.load(std::memory_order_relaxed),
        mpp_cpu_copy_in_fallbacks_.load(std::memory_order_relaxed),
        mpp_early_dropped_frames_.load(std::memory_order_relaxed),
    };
}

PreviewPipelineMetrics& GetPreviewPipelineMetrics() {
    static PreviewPipelineMetrics metrics;
    return metrics;
}

}  // namespace cosmo::media
