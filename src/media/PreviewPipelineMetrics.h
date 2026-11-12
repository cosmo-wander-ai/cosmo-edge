#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace cosmo::media {

struct PreviewPipelineMetricsSnapshot {
    uint64_t active_publishers{0};
    uint64_t active_preview_streams{0};
    uint64_t active_raw_preview_streams{0};
    uint64_t active_algorithm_preview_streams{0};
    uint64_t preview_stream_starts{0};
    uint64_t preview_stream_stops{0};
    uint64_t preview_stream_failures{0};
    uint64_t osd_frames{0};
    uint64_t osd_nanoseconds{0};
    uint64_t published_frames{0};
    uint64_t publish_nanoseconds{0};
    uint64_t first_frames{0};
    uint64_t first_frame_nanoseconds{0};
    uint64_t first_frame_max_nanoseconds{0};
    uint64_t rga_frames{0};
    uint64_t rga_nanoseconds{0};
    uint64_t rga_failures{0};
    uint64_t mpp_encoded_frames{0};
    uint64_t mpp_encode_nanoseconds{0};
    uint64_t mpp_encode_failures{0};
    uint64_t mpp_decoded_frames{0};
    uint64_t mpp_decode_nanoseconds{0};
    uint64_t mpp_decode_failures{0};
    uint64_t mpp_decode_fallbacks{0};
    uint64_t mpp_copy_out_frames{0};
    uint64_t mpp_copy_out_nanoseconds{0};
    uint64_t mpp_copy_out_failures{0};
    uint64_t mpp_rga_copy_out_frames{0};
    uint64_t mpp_rga_copy_out_failures{0};
    uint64_t mpp_cpu_copy_out_fallbacks{0};
    uint64_t mpp_rga_copy_in_frames{0};
    uint64_t mpp_rga_copy_in_failures{0};
    uint64_t mpp_cpu_copy_in_fallbacks{0};
    uint64_t mpp_early_dropped_frames{0};
};

// Independent samples for one shared channel+algorithm preview. The window is
// bounded and represents only measurements made by this preview instance.
struct PreviewDurationAggregate {
    uint64_t count{0};
    uint64_t total_nanoseconds{0};
    uint64_t min_nanoseconds{0};
    uint64_t max_nanoseconds{0};
};

struct PreviewDurationSnapshot {
    PreviewDurationAggregate osd;
    PreviewDurationAggregate encode;
};

class PreviewPipelineMetrics {
public:
    using PreviewToken = uint64_t;
    using SampleTime   = std::chrono::steady_clock::time_point;

    void PublisherOpened();
    void PublisherClosed();
    void PreviewStarted(bool algorithm_preview, uint64_t first_frame_nanoseconds);
    void PreviewStopped(bool algorithm_preview);
    void PreviewFailed();
    void RecordOsdFrame(uint64_t nanoseconds);
    void RecordPublishedFrame(uint64_t nanoseconds);
    void RecordRgaOperation(bool success, uint64_t nanoseconds);
    void RecordMppEncode(bool success, uint64_t nanoseconds);
    void RecordMppDecode(bool success, uint64_t nanoseconds);
    void RecordMppDecodeFallback();
    void RecordMppCopyOut(bool success, uint64_t nanoseconds);
    void RecordMppRgaCopyOut(bool success);
    void RecordMppCpuCopyOutFallback();
    void RecordMppRgaCopyIn(bool success);
    void RecordMppCpuCopyInFallback();
    void RecordMppEarlyDrop();

    [[nodiscard]] PreviewPipelineMetricsSnapshot Snapshot() const;

    // Starting a replacement for the same key invalidates the old token, so a
    // delayed callback or Stop() cannot affect the replacement's samples.
    PreviewToken BeginTimedPreview(const std::string& channel_id, const std::string& algorithm_id);
    void MarkTimedPreviewReady(PreviewToken token);
    void EndTimedPreview(PreviewToken token);
    void RecordTimedOsd(PreviewToken token, uint64_t nanoseconds,
                        SampleTime at = std::chrono::steady_clock::now());
    void RecordTimedEncode(PreviewToken token, uint64_t nanoseconds,
                           SampleTime at = std::chrono::steady_clock::now());
    [[nodiscard]] PreviewDurationSnapshot TimedPreview(
        const std::string& channel_id, const std::string& algorithm_id,
        SampleTime now = std::chrono::steady_clock::now()) const;

private:
    struct TimedSample {
        SampleTime at;
        uint64_t nanoseconds;
    };
    struct TimedPreviewState {
        std::pair<std::string, std::string> key;
        std::deque<TimedSample> osd;
        std::deque<TimedSample> encode;
        bool ready{false};
    };
    void RecordTimedSample(PreviewToken token, uint64_t nanoseconds, SampleTime at, bool osd);
    static PreviewDurationAggregate AggregateTimedSamples(const std::deque<TimedSample>& samples,
                                                          SampleTime now);

    mutable std::mutex timed_mutex_;
    PreviewToken next_timed_token_{0};
    std::map<std::pair<std::string, std::string>, PreviewToken> timed_keys_;
    std::unordered_map<PreviewToken, TimedPreviewState> timed_previews_;

    std::atomic<uint64_t> active_publishers_{0};
    std::atomic<uint64_t> active_preview_streams_{0};
    std::atomic<uint64_t> active_raw_preview_streams_{0};
    std::atomic<uint64_t> active_algorithm_preview_streams_{0};
    std::atomic<uint64_t> preview_stream_starts_{0};
    std::atomic<uint64_t> preview_stream_stops_{0};
    std::atomic<uint64_t> preview_stream_failures_{0};
    std::atomic<uint64_t> osd_frames_{0};
    std::atomic<uint64_t> osd_nanoseconds_{0};
    std::atomic<uint64_t> published_frames_{0};
    std::atomic<uint64_t> publish_nanoseconds_{0};
    std::atomic<uint64_t> first_frames_{0};
    std::atomic<uint64_t> first_frame_nanoseconds_{0};
    std::atomic<uint64_t> first_frame_max_nanoseconds_{0};
    std::atomic<uint64_t> rga_frames_{0};
    std::atomic<uint64_t> rga_nanoseconds_{0};
    std::atomic<uint64_t> rga_failures_{0};
    std::atomic<uint64_t> mpp_encoded_frames_{0};
    std::atomic<uint64_t> mpp_encode_nanoseconds_{0};
    std::atomic<uint64_t> mpp_encode_failures_{0};
    std::atomic<uint64_t> mpp_decoded_frames_{0};
    std::atomic<uint64_t> mpp_decode_nanoseconds_{0};
    std::atomic<uint64_t> mpp_decode_failures_{0};
    std::atomic<uint64_t> mpp_decode_fallbacks_{0};
    std::atomic<uint64_t> mpp_copy_out_frames_{0};
    std::atomic<uint64_t> mpp_copy_out_nanoseconds_{0};
    std::atomic<uint64_t> mpp_copy_out_failures_{0};
    std::atomic<uint64_t> mpp_rga_copy_out_frames_{0};
    std::atomic<uint64_t> mpp_rga_copy_out_failures_{0};
    std::atomic<uint64_t> mpp_cpu_copy_out_fallbacks_{0};
    std::atomic<uint64_t> mpp_rga_copy_in_frames_{0};
    std::atomic<uint64_t> mpp_rga_copy_in_failures_{0};
    std::atomic<uint64_t> mpp_cpu_copy_in_fallbacks_{0};
    std::atomic<uint64_t> mpp_early_dropped_frames_{0};
};

PreviewPipelineMetrics& GetPreviewPipelineMetrics();

}  // namespace cosmo::media
