#include <limits>

#include "catch_amalgamated.hpp"
#include "flow/sensitivity/ResultAccumulationInput.h"

using namespace cosmo;

namespace {
AiDetectRstEl AccumulationTarget() {
    AiDetectRstEl target;
    target.box = {0, 0, 40, 60};
    TargetAreaUnit area;
    area.area_id = "area";
    target.areaSign.areas.push_back(area);
    return target;
}
}  // namespace

TEST_CASE("Result accumulation requires an explicit completed judgment", "[accumulation]") {
    auto target = AccumulationTarget();
    REQUIRE(AssessAccumulationObservation(target, true, false).status == TrackObservation::kSkipped);
    target.bLogicResult = true;
    REQUIRE(AssessAccumulationObservation(target, true, false).status == TrackObservation::kSkipped);
    target.logic_observation = false;
    const auto negative      = AssessAccumulationObservation(target, true, false);
    REQUIRE(negative.status == TrackObservation::kNegative);
    REQUIRE(negative.snapshot_quality == 2400);
    target.logic_observation = true;
    REQUIRE(AssessAccumulationObservation(target, true, false).status == TrackObservation::kPositive);
    REQUIRE(AssessAccumulationObservation(target, false, false).status == TrackObservation::kUnavailable);
}

TEST_CASE("Skipped or unusable logical observations are not negative evidence", "[accumulation]") {
    auto target              = AccumulationTarget();
    target.logic_observation = false;
    SECTION("filtered") {
        target.bFilter = true;
    }
    SECTION("outside area") {
        target.areaSign.areas.clear();
    }
    SECTION("shielded") {
        target.areaSign.shielded_areas = target.areaSign.areas;
    }
    SECTION("no usable snapshot") {
        target.box.width = 0;
    }
    SECTION("missing judgment") {
        target.logic_observation.reset();
    }
    REQUIRE(AssessAccumulationObservation(target, true, false).status == TrackObservation::kSkipped);
}

TEST_CASE("Recognition adapter retains stranger evidence and quality semantics", "[accumulation][stranger]") {
    auto target              = AccumulationTarget();
    target.relatedEl.bActive = true;
    target.face_observation  = {FaceObservationStatus::kUnmatched, 80};
    REQUIRE(AssessAccumulationObservation(target, true, true).status == TrackObservation::kNegative);
    REQUIRE(AssessAccumulationObservation(target, true, true).snapshot_quality == 80);
    target.face_observation.quality = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(AssessAccumulationObservation(target, true, true).status == TrackObservation::kSkipped);
    target.face_observation.status = FaceObservationStatus::kNoFace;
    REQUIRE(AssessAccumulationObservation(target, true, true).status == TrackObservation::kSkipped);
    target.face_observation.status = FaceObservationStatus::kMatched;
    target.bFilter                 = true;
    REQUIRE(AssessAccumulationObservation(target, true, true).status == TrackObservation::kPositive);
    REQUIRE(AssessAccumulationObservation(target, false, true).status == TrackObservation::kUnavailable);
}

TEST_CASE("Both result sources share the per-track positive latch", "[accumulation]") {
    for (const bool recognition : {false, true}) {
        auto target              = AccumulationTarget();
        target.relatedEl.bActive = true;
        target.face_observation  = {FaceObservationStatus::kUnmatched, 80};
        target.logic_observation = false;
        PositiveTrackEvidence evidence;
        for (const int64_t time : {0, 1500, 3000}) {
            evidence.Observe(time, AssessAccumulationObservation(target, true, recognition).status, 200);
        }
        REQUIRE(evidence.CanAlarm(3, 3000));
        target.face_observation.status = FaceObservationStatus::kMatched;
        target.logic_observation       = true;
        // A success always latches, even inside the sample interval.
        evidence.Observe(3001, AssessAccumulationObservation(target, true, recognition).status, 200);
        REQUIRE_FALSE(evidence.CanAlarm(3, 3000));
    }
}
