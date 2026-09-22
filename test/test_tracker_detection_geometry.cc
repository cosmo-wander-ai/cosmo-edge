#include <algorithm>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "infer/AiTrackerUnify.h"
#include "nn/utils/tracker_wrap.h"

namespace cosmo {
namespace {

    nn::TrackingBox IndexedDetection(float x, int source_index, float confidence = 0.9f) {
        nn::TrackingBox detection;
        detection.class_id               = 1;
        detection.box                    = nn::Rect2f(x, 100.0f, 40.0f, 40.0f);
        detection.confidence             = confidence;
        detection.source_detection_index = source_index;
        return detection;
    }

    std::vector<nn::TrackingBox> TraceIndexed(nn::TrackerWrap& tracker,
                                              const std::vector<nn::TrackingBox>& input) {
        std::vector<nn::TrackingBox> output;
        REQUIRE(static_cast<int>(tracker.Trace(input, output)) == nn::COSMO_NN_OK);
        return output;
    }

    AiDetectRstEl GeometryDetection(const std::string& id, int x, float confidence = 0.9f,
                                    float inset = 5.0f) {
        AiDetectRstEl detection;
        detection.targetId         = id;
        detection.box.x            = x;
        detection.box.y            = 100;
        detection.box.width        = 40;
        detection.box.height       = 40;
        detection.confidence       = {"vehicle", "obb", confidence};
        const float left           = static_cast<float>(x);
        detection.oriented_corners = util::Quad{{{left, 100.0f + inset},
                                                 {left + 40.0f - inset, 100.0f},
                                                 {left + 40.0f, 140.0f - inset},
                                                 {left + inset, 140.0f}}};
        return detection;
    }

    std::vector<AiDetectRstEl> TraceGeometry(AiTrackerUnify& tracker, std::vector<AiDetectRstEl> input) {
        std::vector<AiDetectRstEl> output;
        REQUIRE(tracker.Trace(input, output) == util::ErrorEnum::Success);
        return output;
    }

    const AiDetectRstEl& ByTargetId(const std::vector<AiDetectRstEl>& output, const std::string& id) {
        const auto found = std::find_if(output.begin(), output.end(),
                                        [&](const auto& detection) { return detection.targetId == id; });
        REQUIRE(found != output.end());
        return *found;
    }

    void RequireMeasurement(const AiDetectRstEl& output, const AiDetectRstEl& input) {
        REQUIRE(output.targetId == input.targetId);
        REQUIRE(output.box.x == input.box.x);
        REQUIRE(output.box.y == input.box.y);
        REQUIRE(output.box.width == input.box.width);
        REQUIRE(output.box.height == input.box.height);
        REQUIRE(output.oriented_corners.has_value());
        REQUIRE(input.oriented_corners.has_value());
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE((*output.oriented_corners)[i].x == Catch::Approx((*input.oriented_corners)[i].x));
            REQUIRE((*output.oriented_corners)[i].y == Catch::Approx((*input.oriented_corners)[i].y));
        }
    }

    TEST_CASE("TrackingBox retains caller indices through confidence splits and new tracks",
              "[nn][tracker][geometry]") {
        nn::TrackerWrap tracker;
        nn::TrackerConfig config;
        config.thresh     = {0.5f};
        config.thresh_low = {0.3f};
        config.min_hits   = 0;
        config.motion_iou = 0;
        REQUIRE(static_cast<int>(tracker.SetTrackerConfig(config)) == nn::COSMO_NN_OK);

        REQUIRE(TraceIndexed(tracker, {IndexedDetection(100.0f, 3), IndexedDetection(300.0f, 7)}).empty());
        auto output =
            TraceIndexed(tracker, {IndexedDetection(300.0f, 11, 0.4f), IndexedDetection(100.0f, 13)});
        REQUIRE(output.size() == 2);
        for (const auto& track : output) {
            if (track.box.x == 100.0f) {
                REQUIRE(track.source_detection_index == 13);
            } else {
                REQUIRE(track.box.x == 300.0f);
                REQUIRE(track.source_detection_index == 11);
                REQUIRE(track.confidence == Catch::Approx(0.4f));
            }
        }

        // Both existing tracks have high-confidence matches before adding a
        // third detection. This makes the new track genuinely unmatched in the
        // first association pass, independently of the low-confidence pass.
        output = TraceIndexed(tracker, {IndexedDetection(300.0f, 19), IndexedDetection(500.0f, 17),
                                        IndexedDetection(100.0f, 23)});
        REQUIRE(output.size() == 3);
        for (const auto& track : output) {
            if (track.box.x == 100.0f) {
                REQUIRE(track.source_detection_index == 23);
            } else if (track.box.x == 300.0f) {
                REQUIRE(track.source_detection_index == 19);
            } else {
                REQUIRE(track.box.x == 500.0f);
                REQUIRE(track.source_detection_index == 17);
                REQUIRE(track.status == nn::TrackingStatus::kNew);
            }
        }

        output = TraceIndexed(tracker, {});
        REQUIRE(output.size() == 3);
        for (const auto& track : output) {
            REQUIRE(track.status == nn::TrackingStatus::kLoss);
            REQUIRE(track.source_detection_index == -1);
        }
    }

    TEST_CASE("AiTrackerUnify associates duplicate horizontal boxes with distinct measured geometry",
              "[infer][tracker][geometry]") {
        AiTrackerUnify tracker("obb", {"vehicle"}, {{"vehicle", "obb", 0.5f}}, 640, 480);
        auto first  = GeometryDetection("first", 100, 0.9f, 5.0f);
        auto second = GeometryDetection("second", 100, 0.9f, 15.0f);
        REQUIRE(TraceGeometry(tracker, {first, second}).empty());

        first.targetId    = "current-first";
        second.targetId   = "current-second";
        first.frameIndex  = 99;
        first.bFilter     = true;
        first.filterDesc  = "unrelated detection state";
        const auto output = TraceGeometry(tracker, {second, first});
        REQUIRE(output.size() == 2);
        RequireMeasurement(ByTargetId(output, first.targetId), first);
        RequireMeasurement(ByTargetId(output, second.targetId), second);
        REQUIRE(output[0].trackId != output[1].trackId);
        for (const auto& result : output) {
            REQUIRE(result.confidence.atomic_code == "obb");
            REQUIRE(result.trackStatus == AITrackingStatus::TRACKING);
            REQUIRE(result.frameIndex == 0);
            REQUIRE_FALSE(result.bFilter);
            REQUIRE(result.filterDesc.empty());
        }
    }

    TEST_CASE("AiTrackerUnify preserves original indices when labels and confidence reorder detections",
              "[infer][tracker][geometry]") {
        AiTrackerUnify tracker("obb", {"vehicle"}, {{"vehicle", "obb", 0.5f}}, 640, 480);
        auto left  = GeometryDetection("left", 100);
        auto right = GeometryDetection("right", 300);
        REQUIRE(TraceGeometry(tracker, {left, right}).empty());
        const auto initial = TraceGeometry(tracker, {left, right});
        REQUIRE(initial.size() == 2);
        const int left_track  = ByTargetId(initial, left.targetId).trackId;
        const int right_track = ByTargetId(initial, right.targetId).trackId;

        auto ignored             = GeometryDetection("unknown-label", 100);
        ignored.confidence.label = "not-configured";
        left                     = GeometryDetection("current-left", 101, 0.9f, 10.0f);
        right                    = GeometryDetection("current-right", 301, 0.4f, 12.0f);
        const auto output        = TraceGeometry(tracker, {right, ignored, left});
        REQUIRE(output.size() == 2);
        const auto& tracked_left  = ByTargetId(output, left.targetId);
        const auto& tracked_right = ByTargetId(output, right.targetId);
        RequireMeasurement(tracked_left, left);
        RequireMeasurement(tracked_right, right);
        REQUIRE(tracked_left.trackId == left_track);
        REQUIRE(tracked_right.trackId == right_track);
        REQUIRE(tracked_right.confidence.confidence == Catch::Approx(0.4f));
    }

    TEST_CASE("AiTrackerUnify drops geometry on loss and restores only the current measurement",
              "[infer][tracker][geometry]") {
        AiTrackerUnify tracker("obb", {"vehicle"}, {{"vehicle", "obb", 0.5f}}, 640, 480);
        auto detection = GeometryDetection("initial", 100);
        REQUIRE(TraceGeometry(tracker, {detection}).empty());
        const auto tracked = TraceGeometry(tracker, {detection});
        REQUIRE(tracked.size() == 1);
        const int track_id = tracked.front().trackId;
        RequireMeasurement(tracked.front(), detection);

        auto rejected   = GeometryDetection("rejected-low-confidence", 100, 0.1f, 15.0f);
        const auto lost = TraceGeometry(tracker, {rejected});
        REQUIRE(lost.size() == 1);
        REQUIRE(lost.front().trackId == track_id);
        REQUIRE(lost.front().trackStatus == AITrackingStatus::LOSS);
        REQUIRE_FALSE(lost.front().oriented_corners.has_value());
        REQUIRE_FALSE(lost.front().targetId.empty());
        REQUIRE(lost.front().targetId != detection.targetId);
        REQUIRE(lost.front().targetId != rejected.targetId);

        const auto still_lost = TraceGeometry(tracker, {});
        REQUIRE(still_lost.size() == 1);
        REQUIRE_FALSE(still_lost.front().oriented_corners.has_value());

        detection             = GeometryDetection("reappeared", 101, 0.9f, 17.0f);
        const auto reappeared = TraceGeometry(tracker, {detection});
        REQUIRE(reappeared.size() == 1);
        REQUIRE(reappeared.front().trackId == track_id);
        REQUIRE(reappeared.front().trackStatus == AITrackingStatus::TRACKING);
        RequireMeasurement(reappeared.front(), detection);

        detection.targetId = "axis-aligned";
        detection.oriented_corners.reset();
        const auto axis_aligned = TraceGeometry(tracker, {detection});
        REQUIRE(axis_aligned.size() == 1);
        REQUIRE(axis_aligned.front().targetId == detection.targetId);
        REQUIRE_FALSE(axis_aligned.front().oriented_corners.has_value());
    }

}  // namespace
}  // namespace cosmo
