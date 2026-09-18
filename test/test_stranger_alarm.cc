#include <limits>

#include "catch_amalgamated.hpp"
#include "flow/recognizer/FaceObservationPolicy.h"
#include "flow/sensitivity/PositiveTrackEvidence.h"
#include "flow/target/PersonFaceAssociation.h"

using namespace cosmo;

namespace {
AiDetectRstEl Person(int x = 0) {
    AiDetectRstEl person;
    person.box         = {x, 0, 160, 400};
    person.trackId     = x + 1;
    person.trackIdInfo = "track-" + std::to_string(x);
    return person;
}
AiDetectRstEl Face(int x = 30, int size = 80) {
    AiDetectRstEl face;
    face.box                   = {x, 20, size, size};
    face.confidence.label      = "face";
    face.confidence.confidence = 0.9f;
    return face;
}
AiDetectRstEl GoodFace() {
    auto person              = Person();
    person.relatedEl.bActive = true;
    person.relatedEl.landmark.landmark.resize(5);
    for (const auto& label : {"faceBlur0", "faceBlur1", "faceBlur2", "frontFace"}) {
        AiConfidence confidence;
        confidence.label = label;
        confidence.confidence =
            std::string(label) == "faceBlur0" || std::string(label) == "faceBlur1" ? 0.0f : 1.0f;
        person.relatedEl.classifyRst.push_back(confidence);
    }
    return person;
}
}  // namespace

TEST_CASE("Stranger evidence requires repeated valid comparisons and completed presence", "[stranger]") {
    PositiveTrackEvidence state;
    REQUIRE_FALSE(state.HasExpired(10000, 1000));
    REQUIRE_FALSE(state.CanAlarm(3, 3000));
    state.Observe(0, TrackObservation::kNegative, 200);
    state.Observe(1000, TrackObservation::kNegative, 200);
    REQUIRE_FALSE(state.CanAlarm(3, 3000));
    state.Observe(3000, TrackObservation::kNegative, 200);
    REQUIRE(state.CanAlarm(3, 3000));
    REQUIRE_FALSE(state.HasExpired(3999, 1000));
    REQUIRE(state.HasExpired(4000, 1000));
}

TEST_CASE("Any successful face match permanently saves its track", "[stranger]") {
    for (int positive_at : {0, 1, 3}) {
        PositiveTrackEvidence state;
        for (int i = 0; i < 5; ++i) {
            state.Observe(i * 1000,
                          i == positive_at ? TrackObservation::kPositive : TrackObservation::kNegative, 200);
        }
        REQUIRE(state.IsPositive());
        REQUIRE_FALSE(state.CanAlarm(3, 3000));
        state.Invalidate();
        REQUIRE(state.IsPositive());
        REQUIRE_FALSE(state.CanAlarm(1, 0));
    }
}

TEST_CASE("Skipped faces keep tracks alive without counting failures", "[stranger]") {
    PositiveTrackEvidence state;
    state.Observe(0, TrackObservation::kNegative, 200);
    for (int i = 1; i <= 20; ++i) {
        state.Observe(i * 200, TrackObservation::kSkipped, 200);
    }
    REQUIRE_FALSE(state.CanAlarm(3, 3000));
    REQUIRE_FALSE(state.HasExpired(4500, 1000));
    REQUIRE(state.HasExpired(5000, 1000));
}

TEST_CASE("Duplicate delayed and oversampled observations cannot inflate evidence", "[stranger]") {
    PositiveTrackEvidence state;
    state.Observe(-1, TrackObservation::kNegative, 0);
    state.Observe(0, TrackObservation::kNegative, 200);
    state.Observe(0, TrackObservation::kNegative, 0);
    state.Observe(100, TrackObservation::kNegative, 200);
    state.Observe(50, TrackObservation::kNegative, 0);
    REQUIRE_FALSE(state.CanAlarm(2, 0));
    state.Observe(200, TrackObservation::kNegative, 200);
    REQUIRE(state.CanAlarm(2, 0));
    REQUIRE_FALSE(state.CanAlarm(3, 0));
}

TEST_CASE("Incomplete recognition prevents negative conclusions for the track", "[stranger]") {
    PositiveTrackEvidence state;
    state.Observe(0, TrackObservation::kNegative, 0);
    state.Observe(1000, TrackObservation::kUnavailable, 0);
    state.Observe(2000, TrackObservation::kNegative, 0);
    state.Observe(3000, TrackObservation::kNegative, 0);
    REQUIRE_FALSE(state.CanAlarm(3, 3000));
    REQUIRE(state.HasExpired(4000, 1000));
}

TEST_CASE("Track evidence is independent and supports fresh UUID lifetimes", "[stranger]") {
    PositiveTrackEvidence known, stranger, fresh;
    known.Observe(0, TrackObservation::kPositive, 0);
    for (int i = 0; i <= 3; ++i) {
        stranger.Observe(i * 1000, TrackObservation::kNegative, 0);
    }
    REQUIRE_FALSE(known.CanAlarm(1, 0));
    REQUIRE(stranger.CanAlarm(3, 3000));
    REQUIRE_FALSE(fresh.CanAlarm(3, 0));
}

TEST_CASE("Person face association retains independent pedestrian identities", "[stranger]") {
    std::vector<AiDetectRstEl> people{Person(), Person(200), Person(400)};
    AssociatePersonFaces(people, {Face(), Face(230)}, 60);
    REQUIRE(people.size() == 3);
    REQUIRE(people[0].relatedEl.bActive);
    REQUIRE(people[1].relatedEl.bActive);
    REQUIRE(people[1].relatedEl.box.x == 230);
    REQUIRE(people[0].trackIdInfo == "track-0");
    REQUIRE_FALSE(people[2].relatedEl.bActive);
    REQUIRE(people[2].bFilter);
    REQUIRE(people[2].face_observation.status == FaceObservationStatus::kNoFace);
}

TEST_CASE("Ambiguous faces never transfer a known identity to another pedestrian", "[stranger]") {
    SECTION("overlapping people") {
        std::vector<AiDetectRstEl> people{Person(), Person(10)};
        AssociatePersonFaces(people, {Face()}, 60);
        REQUIRE_FALSE(people[0].relatedEl.bActive);
        REQUIRE_FALSE(people[1].relatedEl.bActive);
    }
    SECTION("multiple faces within one body") {
        std::vector<AiDetectRstEl> people{Person()};
        AssociatePersonFaces(people, {Face(), Face(70)}, 60);
        REQUIRE_FALSE(people[0].relatedEl.bActive);
    }
    SECTION("face near feet") {
        std::vector<AiDetectRstEl> people{Person()};
        auto face  = Face();
        face.box.y = 280;
        AssociatePersonFaces(people, {face}, 60);
        REQUIRE_FALSE(people[0].relatedEl.bActive);
    }
}

TEST_CASE("Small missing and lost faces remain skipped observations", "[stranger]") {
    std::vector<AiDetectRstEl> people{Person()};
    SECTION("small") {
        AssociatePersonFaces(people, {Face(30, 40)}, 60);
        REQUIRE(people[0].relatedEl.bActive);
        REQUIRE(people[0].bFilter);
        REQUIRE(people[0].face_observation.status == FaceObservationStatus::kLowQuality);
    }
    SECTION("lost") {
        people[0].trackStatus = AITrackingStatus::LOSS;
        AssociatePersonFaces(people, {Face()}, 60);
        REQUIRE_FALSE(people[0].relatedEl.bActive);
    }
    SECTION("stale related face") {
        people[0] = GoodFace();
        AssociatePersonFaces(people, {}, 60);
        REQUIRE_FALSE(people[0].relatedEl.bActive);
        REQUIRE(people[0].relatedEl.landmark.landmark.empty());
    }
}

TEST_CASE("Only qualified associated faces reach comparison", "[stranger]") {
    auto target = GoodFace();
    REQUIRE(AssessFaceObservation(target, false, 60, true).status == FaceObservationStatus::kNotObserved);
    SECTION("no association") {
        target.relatedEl.bActive = false;
        REQUIRE(AssessFaceObservation(target, false, 60, true).status == FaceObservationStatus::kNoFace);
    }
    SECTION("filtered") {
        target.bFilter = true;
        REQUIRE(AssessFaceObservation(target, false, 60, true).status == FaceObservationStatus::kFiltered);
    }
    SECTION("outside configured region") {
        REQUIRE(AssessFaceObservation(target, true, 60, true).status == FaceObservationStatus::kFiltered);
    }
    SECTION("missing landmarks") {
        target.relatedEl.landmark.landmark.clear();
        REQUIRE(AssessFaceObservation(target, false, 60, true).status == FaceObservationStatus::kUnavailable);
    }
    SECTION("poor quality") {
        target.relatedEl.classifyRst[2].confidence = 0.1f;
        REQUIRE(AssessFaceObservation(target, false, 60, true).status == FaceObservationStatus::kLowQuality);
    }
    SECTION("invalid angle confidence") {
        target.relatedEl.classifyRst.back().confidence = std::numeric_limits<float>::infinity();
        REQUIRE(AssessFaceObservation(target, false, 60, true).status == FaceObservationStatus::kLowQuality);
    }
    SECTION("side face") {
        target.relatedEl.classifyRst.back().label = "leftFace";
        REQUIRE(AssessFaceObservation(target, false, 60, true).status == FaceObservationStatus::kLowQuality);
        REQUIRE(AssessFaceObservation(target, false, 60, false).status ==
                FaceObservationStatus::kNotObserved);
    }
}

TEST_CASE("Empty failed or partial library searches are not negative evidence", "[stranger]") {
    AiDetectMatchHighScoreInfo info;
    REQUIRE(FaceComparisonStatus(info) == FaceObservationStatus::kUnavailable);
    info.person_id    = "person";
    info.match_degree = 50;
    info.setPicCount  = -1;
    REQUIRE(FaceComparisonStatus(info) == FaceObservationStatus::kUnavailable);
    info.setPicCount = 0;
    REQUIRE(FaceComparisonStatus(info) == FaceObservationStatus::kUnavailable);
    info.setPicCount = 2;
    REQUIRE(FaceComparisonStatus(info) == FaceObservationStatus::kUnmatched);
    info.matched = true;
    REQUIRE(FaceComparisonStatus(info) == FaceObservationStatus::kMatched);
    info.setPicCount = -1;
    REQUIRE(FaceComparisonStatus(info) == FaceObservationStatus::kMatched);
    info.match_degree = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(FaceComparisonStatus(info) == FaceObservationStatus::kUnavailable);
}
