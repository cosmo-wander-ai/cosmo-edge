#include <algorithm>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "infer/AiTrackerUnify.h"

namespace cosmo {
namespace {

    using nn::ClassMatchMode;

    AiTrackerUnify Tracker(ClassMatchMode mode) {
        return AiTrackerUnify("det", {"person", "call", "phone", "vehicle"},
                              {{"person", "det", 0.5f},
                               {"call", "det", 0.5f},
                               {"phone", "det", 0.5f},
                               {"vehicle", "det", 0.5f}},
                              640, 480, mode);
    }

    AiDetectRstEl Detection(const std::string& id, const std::string& label, float score = 0.9f,
                            int x = 100) {
        AiDetectRstEl result;
        result.targetId   = id;
        result.box.x      = x;
        result.box.y      = 100;
        result.box.width  = 40;
        result.box.height = 40;
        result.confidence = {label, "det", score, AiAnnotationSource::Detection};
        return result;
    }

    std::vector<AiDetectRstEl> Trace(AiTrackerUnify& tracker, std::vector<AiDetectRstEl> input) {
        std::vector<AiDetectRstEl> output;
        REQUIRE(tracker.Trace(input, output) == util::ErrorEnum::Success);
        return output;
    }

    int Confirm(AiTrackerUnify& tracker, const std::string& label = "person") {
        REQUIRE(Trace(tracker, {Detection("first", label)}).empty());
        const auto confirmed = Trace(tracker, {Detection("confirmed", label)});
        REQUIRE(confirmed.size() == 1);
        REQUIRE(confirmed.front().trackStatus == AITrackingStatus::TRACKING);
        return confirmed.front().trackId;
    }

    void RequireObservation(const std::vector<AiDetectRstEl>& output, int track_id,
                            const AiDetectRstEl& detection) {
        const auto found = std::find_if(output.begin(), output.end(),
                                        [&](const auto& item) { return item.trackId == track_id; });
        REQUIRE(found != output.end());
        REQUIRE(found->trackStatus == AITrackingStatus::TRACKING);
        REQUIRE(found->targetId == detection.targetId);
        REQUIRE(found->confidence.label == detection.confidence.label);
        REQUIRE(found->confidence.confidence == detection.confidence.confidence);
        REQUIRE(found->confidence.source == AiAnnotationSource::Detection);
    }

    TEST_CASE("Ignore class reports each matched behavior on the original track", "[tracker][class-mode]") {
        auto tracker     = Tracker(ClassMatchMode::kIgnoreClass);
        const int id     = Confirm(tracker);
        const auto call  = Detection("on-call", "call");
        const auto phone = Detection("using-phone", "phone", 0.4f);
        RequireObservation(Trace(tracker, {call}), id, call);
        RequireObservation(Trace(tracker, {phone}), id, phone);
    }

    TEST_CASE("Same class only reserves the track for a matching observation in either pass",
              "[tracker][class-mode]") {
        auto tracker = Tracker(ClassMatchMode::kSameClassOnly);
        const int id = Confirm(tracker);
        REQUIRE(tracker.SetConfig(0.7f, 4, 0.9) == util::ErrorEnum::Success);

        SECTION("Single candidate") {
            const float score    = GENERATE(0.9f, 0.4f);
            const auto candidate = Detection("current", GENERATE("person", "vehicle"), score);
            const auto output    = Trace(tracker, {candidate});
            REQUIRE(output.size() == 1);
            if (candidate.confidence.label == "person") {
                RequireObservation(output, id, candidate);
            } else {
                REQUIRE(output.front().trackId == id);
                REQUIRE(output.front().trackStatus == AITrackingStatus::LOSS);
                REQUIRE(output.front().targetId != candidate.targetId);
                REQUIRE(output.front().confidence.source == AiAnnotationSource::Unknown);
            }
        }

        SECTION("A high-score vehicle and a low-score person both keep their own observations") {
            const auto vehicle = Detection("car", "vehicle");
            const auto person  = Detection("walker", "person", 0.4f, 105);
            RequireObservation(Trace(tracker, {vehicle, person}), id, person);
            const auto next = Trace(tracker, {vehicle, person});
            REQUIRE(next.size() == 2);
            RequireObservation(next, id, person);
            const int vehicle_id = next[0].trackId == id ? next[1].trackId : next[0].trackId;
            REQUIRE(vehicle_id != id);
            RequireObservation(next, vehicle_id, vehicle);
        }

        SECTION("A same-class distance rejection keeps the existing high-score handling") {
            const auto far = Detection("far", "person", 0.9f, 500);
            for (int frame = 0; frame < 2; ++frame) {
                const auto output = Trace(tracker, {far});
                REQUIRE(output.size() == 1);
                REQUIRE(output.front().trackId == id);
                REQUIRE(output.front().trackStatus == AITrackingStatus::LOSS);
            }
        }

        SECTION("The nearest wrong-class box cannot displace an eligible box") {
            const float score  = GENERATE(0.9f, 0.4f);
            const auto vehicle = Detection("car", "vehicle", score);
            const auto person  = Detection("walker", "person", score, 105);
            RequireObservation(Trace(tracker, {vehicle, person}), id, person);
        }

        SECTION("Another node can ignore class independently") {
            auto other         = Tracker(ClassMatchMode::kIgnoreClass);
            const int other_id = Confirm(other);
            const auto vehicle = Detection("current", "vehicle");
            RequireObservation(Trace(other, {vehicle}), other_id, vehicle);
            const auto output = Trace(tracker, {vehicle});
            REQUIRE(output.size() == 1);
            REQUIRE(output.front().trackStatus == AITrackingStatus::LOSS);
        }
    }

    TEST_CASE("Ignore class counts consecutive low scores against the current detection threshold",
              "[tracker][class-mode]") {
        auto tracker                    = Tracker(ClassMatchMode::kIgnoreClass);
        const std::string initial_label = GENERATE("person", "call");
        const int id                    = Confirm(tracker, initial_label);
        REQUIRE(tracker.SetConfidence(AiConfidence{"call", "det", 0.8f}) == util::ErrorEnum::Success);
        for (int frame = 1; frame <= 16; ++frame) {
            CAPTURE(initial_label, frame);
            const auto low    = Detection(std::to_string(frame), "call", 0.6f);
            const auto output = Trace(tracker, {low});
            if (frame <= 15) {
                RequireObservation(output, id, low);
            } else {
                REQUIRE(output.empty());
                REQUIRE(Trace(tracker, {low}).empty());
                REQUIRE(Trace(tracker, {Detection("new-seed", "call")}).empty());
            }
        }
    }

    TEST_CASE("Ignoring class preserves one-to-one observations across a label swap",
              "[tracker][class-mode]") {
        const float score             = GENERATE(0.4f, 0.9f);
        const int separation          = GENERATE(0, 8);
        const bool should_swap_labels = GENERATE(false, true);
        auto tracker                  = Tracker(ClassMatchMode::kIgnoreClass);
        auto left                     = Detection("left-seed", "person");
        auto right                    = Detection("right-seed", "call", 0.9f, 100 + separation);
        REQUIRE(Trace(tracker, {left, right}).empty());
        const auto initial = Trace(tracker, {left, right});
        REQUIRE(initial.size() == 2);
        const int left_id  = (initial[0].targetId == left.targetId ? initial[0] : initial[1]).trackId;
        const int right_id = (initial[0].targetId == right.targetId ? initial[0] : initial[1]).trackId;
        REQUIRE(left_id != right_id);
        left  = Detection("left-current", should_swap_labels ? "call" : "person", score);
        right = Detection("right-current", should_swap_labels ? "person" : "call", score, 100 + separation);
        const auto output = Trace(tracker, {right, left});
        REQUIRE(output.size() == 2);
        const bool is_exchanged =
            separation == 0 &&
            ((output[0].trackId == left_id ? output[0] : output[1]).targetId == right.targetId);
        RequireObservation(output, left_id, is_exchanged ? right : left);
        RequireObservation(output, right_id, is_exchanged ? left : right);
    }

    TEST_CASE("Ignore class rejects observations outside the existing tracking gates",
              "[tracker][class-mode]") {
        auto tracker = Tracker(ClassMatchMode::kIgnoreClass);
        const int id = Confirm(tracker);
        std::vector<AiDetectRstEl> input;
        SECTION("Out of range") {
            input = {Detection("far", "call", 0.9f, 500)};
        }
        SECTION("Below the low score cutoff") {
            input = {Detection("weak", "call", 0.1f)};
        }
        SECTION("Unknown label") {
            input = {Detection("unknown", "unknown")};
        }
        SECTION("Empty frame") {}
        const auto output = Trace(tracker, input);
        REQUIRE(output.size() == 1);
        REQUIRE(output.front().trackId == id);
        REQUIRE(output.front().trackStatus == AITrackingStatus::LOSS);
        REQUIRE(output.front().confidence.source == AiAnnotationSource::Unknown);
        if (!input.empty())
            REQUIRE(output.front().targetId != input.front().targetId);
    }

    TEST_CASE("Legacy classid_same retains its high-confidence assignment preference",
              "[tracker][class-mode]") {
        nn::TrackerWrap tracker;
        nn::TrackerConfig config;
        config.classid_same = GENERATE(false, true);
        config.thresh       = {0.5f, 0.5f};
        config.thresh_low   = {0.35f, 0.35f};
        REQUIRE(static_cast<int>(tracker.SetTrackerConfig(config)) == nn::COSMO_NN_OK);
        nn::TrackingBox left;
        left.box                     = nn::Rect2f(100, 100, 40, 40);
        left.confidence              = 0.9f;
        left.class_id                = 1;
        left.source_detection_index  = 0;
        auto right                   = left;
        right.box.x                  = 112;
        right.class_id               = 2;
        right.source_detection_index = 1;

        std::vector<nn::TrackingBox> output;
        REQUIRE(static_cast<int>(tracker.Trace({left, right}, output)) == nn::COSMO_NN_OK);
        REQUIRE(static_cast<int>(tracker.Trace({left, right}, output)) == nn::COSMO_NN_OK);
        REQUIRE(output.size() == 2);
        const int left_id = (output[0].source_detection_index == 0 ? output[0] : output[1]).id;
        std::swap(left.class_id, right.class_id);
        REQUIRE(static_cast<int>(tracker.Trace({left, right}, output)) == nn::COSMO_NN_OK);
        REQUIRE(output.size() == 2);
        const auto& tracked = output[0].id == left_id ? output[0] : output[1];
        REQUIRE(tracked.id == left_id);
        REQUIRE(tracked.source_detection_index == (config.classid_same ? 1 : 0));
    }

}  // namespace
}  // namespace cosmo
