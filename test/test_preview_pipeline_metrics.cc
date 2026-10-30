#include <atomic>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "media/PreviewPipelineMetrics.h"

TEST_CASE("Preview pipeline metrics expose lifecycle and stage timings", "[media][preview][metrics]") {
    cosmo::media::PreviewPipelineMetrics metrics;

    metrics.PublisherOpened();
    metrics.PreviewStarted(false, 10'000'000);
    metrics.PreviewStarted(true, 25'000'000);
    metrics.RecordOsdFrame(2'000'000);
    metrics.RecordPublishedFrame(3'000'000);
    metrics.RecordRgaOperation(true, 4'000'000);
    metrics.RecordRgaOperation(false, 5'000'000);
    metrics.RecordMppEncode(true, 6'000'000);
    metrics.RecordMppEncode(false, 7'000'000);
    metrics.RecordMppDecode(true, 8'000'000);
    metrics.RecordMppDecode(false, 9'000'000);
    metrics.RecordMppDecodeFallback();
    metrics.RecordMppCopyOut(true, 11'000'000);
    metrics.RecordMppCopyOut(false, 12'000'000);
    metrics.RecordMppRgaCopyOut(true);
    metrics.RecordMppRgaCopyOut(false);
    metrics.RecordMppCpuCopyOutFallback();
    metrics.RecordMppRgaCopyIn(true);
    metrics.RecordMppRgaCopyIn(false);
    metrics.RecordMppCpuCopyInFallback();
    metrics.RecordMppEarlyDrop();
    metrics.PreviewFailed();

    auto during = metrics.Snapshot();
    CHECK(during.active_publishers == 1);
    CHECK(during.active_preview_streams == 2);
    CHECK(during.active_raw_preview_streams == 1);
    CHECK(during.active_algorithm_preview_streams == 1);
    CHECK(during.preview_stream_starts == 2);
    CHECK(during.preview_stream_failures == 1);
    CHECK(during.osd_frames == 1);
    CHECK(during.osd_nanoseconds == 2'000'000);
    CHECK(during.published_frames == 1);
    CHECK(during.publish_nanoseconds == 3'000'000);
    CHECK(during.first_frames == 2);
    CHECK(during.first_frame_nanoseconds == 35'000'000);
    CHECK(during.first_frame_max_nanoseconds == 25'000'000);
    CHECK(during.rga_frames == 1);
    CHECK(during.rga_nanoseconds == 4'000'000);
    CHECK(during.rga_failures == 1);
    CHECK(during.mpp_encoded_frames == 1);
    CHECK(during.mpp_encode_nanoseconds == 6'000'000);
    CHECK(during.mpp_encode_failures == 1);
    CHECK(during.mpp_decoded_frames == 1);
    CHECK(during.mpp_decode_nanoseconds == 8'000'000);
    CHECK(during.mpp_decode_failures == 1);
    CHECK(during.mpp_decode_fallbacks == 1);
    CHECK(during.mpp_copy_out_frames == 1);
    CHECK(during.mpp_copy_out_nanoseconds == 11'000'000);
    CHECK(during.mpp_copy_out_failures == 1);
    CHECK(during.mpp_rga_copy_out_frames == 1);
    CHECK(during.mpp_rga_copy_out_failures == 1);
    CHECK(during.mpp_cpu_copy_out_fallbacks == 1);
    CHECK(during.mpp_rga_copy_in_frames == 1);
    CHECK(during.mpp_rga_copy_in_failures == 1);
    CHECK(during.mpp_cpu_copy_in_fallbacks == 1);
    CHECK(during.mpp_early_dropped_frames == 1);

    metrics.PreviewStopped(false);
    metrics.PreviewStopped(true);
    metrics.PublisherClosed();
    metrics.PublisherClosed();

    auto after = metrics.Snapshot();
    CHECK(after.active_publishers == 0);
    CHECK(after.active_preview_streams == 0);
    CHECK(after.active_raw_preview_streams == 0);
    CHECK(after.active_algorithm_preview_streams == 0);
    CHECK(after.preview_stream_stops == 2);
}

TEST_CASE("Preview timings isolate streams, expire samples and reject obsolete generations",
          "[media][preview][metrics]") {
    using namespace std::chrono_literals;
    cosmo::media::PreviewPipelineMetrics metrics;
    const auto start = std::chrono::steady_clock::time_point{} + 100s;
    const auto a     = metrics.BeginTimedPreview("camera-a", "algo-a");
    const auto b     = metrics.BeginTimedPreview("camera-b", "algo-a");
    const auto c     = metrics.BeginTimedPreview("camera-a", "algo-b");
    REQUIRE(a != 0);
    REQUIRE(b != 0);
    REQUIRE(c != 0);
    CHECK(metrics.BeginTimedPreview("", "algo-a") == 0);

    metrics.RecordTimedOsd(a, 2'000'000, start);
    metrics.RecordTimedOsd(a, 4'000'000, start + 1s);
    metrics.RecordTimedEncode(a, 7'000'000, start + 2s);
    metrics.RecordTimedOsd(b, 9'000'000, start);
    metrics.RecordTimedEncode(c, 11'000'000, start);
    // Startup measurements are hidden until the shared publisher is ready.
    CHECK(metrics.TimedPreview("camera-a", "algo-a", start + 3s).osd.count == 0);
    metrics.MarkTimedPreviewReady(a);
    metrics.MarkTimedPreviewReady(b);
    metrics.MarkTimedPreviewReady(c);
    auto snap = metrics.TimedPreview("camera-a", "algo-a", start + 3s);
    CHECK(snap.osd.count == 2);
    CHECK(snap.osd.total_nanoseconds == 6'000'000);
    CHECK(snap.encode.count == 1);
    CHECK(snap.encode.total_nanoseconds == 7'000'000);
    CHECK(metrics.TimedPreview("camera-b", "algo-a", start + 3s).osd.total_nanoseconds == 9'000'000);
    CHECK(metrics.TimedPreview("camera-b", "algo-a", start + 3s).encode.count == 0);
    CHECK(metrics.TimedPreview("camera-a", "algo-b", start + 3s).encode.total_nanoseconds == 11'000'000);
    CHECK(metrics.TimedPreview("camera-a", "algo-b", start + 3s).osd.count == 0);
    CHECK(metrics.TimedPreview("camera-b", "algo-b", start + 3s).osd.count == 0);

    snap = metrics.TimedPreview("camera-a", "algo-a", start + 5s);
    CHECK(snap.osd.count == 1);
    CHECK(snap.osd.total_nanoseconds == 4'000'000);
    CHECK(metrics.TimedPreview("camera-a", "algo-a", start + 8s).encode.count == 0);

    const auto replacement = metrics.BeginTimedPreview("camera-a", "algo-a");
    REQUIRE(replacement != a);
    CHECK(metrics.TimedPreview("camera-a", "algo-a", start + 3s).osd.count == 0);
    metrics.RecordTimedOsd(replacement, 5'000'000, start + 3s);
    metrics.RecordTimedOsd(a, 99'000'000, start + 3s);
    metrics.EndTimedPreview(a);
    metrics.MarkTimedPreviewReady(a);
    CHECK(metrics.TimedPreview("camera-a", "algo-a", start + 4s).osd.count == 0);
    metrics.MarkTimedPreviewReady(replacement);
    CHECK(metrics.TimedPreview("camera-a", "algo-a", start + 4s).osd.total_nanoseconds == 5'000'000);
    metrics.EndTimedPreview(replacement);
    CHECK(metrics.TimedPreview("camera-a", "algo-a", start + 4s).osd.count == 0);
    CHECK(metrics.TimedPreview("camera-b", "algo-a", start + 4s).osd.count == 1);
    metrics.EndTimedPreview(b);
    metrics.EndTimedPreview(c);
}

TEST_CASE("Preview timing snapshots remain consistent under concurrent samples and reads",
          "[media][preview][metrics]") {
    cosmo::media::PreviewPipelineMetrics metrics;
    const auto token = metrics.BeginTimedPreview("camera", "algorithm");
    metrics.MarkTimedPreviewReady(token);
    const auto at = std::chrono::steady_clock::now();
    std::atomic<bool> consistent{true};
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
        threads.emplace_back([&, worker] {
            for (int i = 0; i < 100; ++i) {
                if (worker < 2)
                    metrics.RecordTimedOsd(token, 1'000'000, at);
                else
                    metrics.RecordTimedEncode(token, 2'000'000, at);
                const auto snap = metrics.TimedPreview("camera", "algorithm", at);
                if (snap.osd.total_nanoseconds != snap.osd.count * 1'000'000 ||
                    snap.encode.total_nanoseconds != snap.encode.count * 2'000'000)
                    consistent.store(false);
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    CHECK(consistent.load());
    const auto snap = metrics.TimedPreview("camera", "algorithm", at);
    CHECK(snap.osd.count == 200);
    CHECK(snap.encode.count == 200);
    metrics.EndTimedPreview(token);
}
