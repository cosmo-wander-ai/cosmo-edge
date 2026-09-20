#include <limits>

#include "catch_amalgamated.hpp"
#include "flow/target/PersonFaceAssociation.h"
#include "flow/target/TargetAssociation.h"

using namespace cosmo;

namespace {
AiDetectRstEl Parent(int x = 0) {
    AiDetectRstEl target;
    target.box              = {x, 0, 200, 400};
    target.trackId          = x;
    target.trackIdInfo      = "parent-" + std::to_string(x);
    target.confidence.label = "vehicle";
    TargetAreaUnit area;
    area.area_id = "area";
    target.areaSign.areas.push_back(area);
    return target;
}
AiDetectRstEl Child(const std::string& label = "plate", int x = 40, int y = 280) {
    AiDetectRstEl target;
    target.box                   = {x, y, 80, 60};
    target.confidence.label      = label;
    target.confidence.confidence = 0.95f;
    return target;
}
TargetAssociationConfig PlateConfig() {
    TargetAssociationConfig config;
    config.labels = {"plate"};
    config.region = AssociationRegion::kLowerHalf;
    return config;
}
MsgDynamicKeyValue Param(const std::string& key, const std::string& value) {
    MsgDynamicKeyValue param;
    param.key   = key;
    param.value = value;
    return param;
}
}  // namespace

TEST_CASE("Generic association supports plates and retains parent identities", "[association]") {
    auto plate              = Child();
    plate.landmark.landmark = {{40, 280}, {40, 340}, {120, 340}, {120, 280}};
    std::vector<AiDetectRstEl> parents{Parent(), Parent(250)};
    AssociateTargets(parents, {plate}, PlateConfig());
    REQUIRE(parents.size() == 2);
    REQUIRE(parents[0].association_status == TargetAssociationStatus::kMatched);
    REQUIRE(parents[0].relatedEl.confidence.label == "plate");
    REQUIRE(parents[0].relatedEl.landmark.landmark.size() == 4);
    REQUIRE(parents[0].trackIdInfo == "parent-0");
    REQUIRE(parents[0].confidence.label == "vehicle");
    REQUIRE(parents[0].box.height == 400);
    REQUIRE(parents[0].areaSign.areas.front().area_id == "area");
    REQUIRE(parents[0].face_observation.status == FaceObservationStatus::kNotObserved);
    REQUIRE(parents[1].association_status == TargetAssociationStatus::kMissing);
    REQUIRE(parents[1].bFilter);
}

TEST_CASE("Association region and label selection determine eligible pairs", "[association]") {
    auto config = PlateConfig();
    SECTION("upper region rejects plate near vehicle bottom") {
        config.region = AssociationRegion::kUpperHalf;
        std::vector<AiDetectRstEl> parents{Parent()};
        AssociateTargets(parents, {Child()}, config);
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kMissing);
    }
    SECTION("upper region can associate a helmet") {
        config.region = AssociationRegion::kUpperHalf;
        config.labels = {"helmet"};
        std::vector<AiDetectRstEl> parents{Parent()};
        AssociateTargets(parents, {Child("helmet", 40, 10), Child("face", 60, 50)}, config);
        REQUIRE(parents[0].relatedEl.confidence.label == "helmet");
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kMatched);
    }
    SECTION("whole region accepts either vertical half") {
        config.region = AssociationRegion::kWhole;
        for (int y : {10, 280}) {
            std::vector<AiDetectRstEl> parents{Parent()};
            AssociateTargets(parents, {Child("plate", 40, y)}, config);
            REQUIRE(parents[0].association_status == TargetAssociationStatus::kMatched);
        }
    }
}

TEST_CASE("Containment uses child area rather than parent IoU", "[association]") {
    auto config = PlateConfig();
    for (int x : {-8, -9}) {
        std::vector<AiDetectRstEl> parents{Parent()};
        AssociateTargets(parents, {Child("plate", x)}, config);
        REQUIRE(parents[0].relatedEl.bActive == (x == -8));
    }
    config.min_containment = 0.8;
    std::vector<AiDetectRstEl> parents{Parent()};
    AssociateTargets(parents, {Child("plate", -9)}, config);
    REQUIRE(parents[0].association_status == TargetAssociationStatus::kMatched);
}

TEST_CASE("Ambiguity never selects a highest score or nearest candidate", "[association]") {
    auto config = PlateConfig();
    SECTION("one child fits two parents") {
        std::vector<AiDetectRstEl> parents{Parent(), Parent(10)};
        AssociateTargets(parents, {Child()}, config);
        for (const auto& parent : parents) {
            REQUIRE(parent.association_status == TargetAssociationStatus::kAmbiguous);
            REQUIRE_FALSE(parent.relatedEl.bActive);
        }
    }
    SECTION("two selected child labels fit one parent") {
        config.labels.push_back("otherPlate");
        std::vector<AiDetectRstEl> parents{Parent()};
        AssociateTargets(parents, {Child(), Child("otherPlate", 80)}, config);
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kAmbiguous);
        REQUIRE_FALSE(parents[0].relatedEl.bActive);
    }
}

TEST_CASE("Association distinguishes failed missing small and filtered observations", "[association]") {
    auto config = PlateConfig();
    std::vector<AiDetectRstEl> parents{Parent()};
    parents[0].relatedEl.bActive         = true;
    parents[0].relatedEl.feature.feature = {1, 2};
    SECTION("detector failure clears stale relation and rejects partial detections") {
        AssociateTargets(parents, {Child()}, config, false);
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kUnavailable);
        REQUIRE_FALSE(parents[0].relatedEl.bActive);
        REQUIRE(parents[0].relatedEl.feature.feature.empty());
    }
    SECTION("no detection is missing rather than failed") {
        AssociateTargets(parents, {}, config);
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kMissing);
    }
    SECTION("undersized detection is present but unusable") {
        config.min_size = 70;
        AssociateTargets(parents, {Child()}, config);
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kTooSmall);
        REQUIRE(parents[0].relatedEl.bActive);
        REQUIRE(parents[0].bFilter);
    }
    SECTION("lost tracks cannot acquire a relation") {
        parents[0].trackStatus = AITrackingStatus::LOSS;
        AssociateTargets(parents, {Child()}, config);
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kFiltered);
        REQUIRE_FALSE(parents[0].relatedEl.bActive);
    }
    SECTION("no selected labels does not fall back to face") {
        config.labels.clear();
        AssociateTargets(parents, {Child("face")}, config);
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kUnavailable);
    }
}

TEST_CASE("Association ignores unselected and low confidence detections", "[association]") {
    for (float confidence : {0.1f, std::numeric_limits<float>::quiet_NaN()}) {
        auto child                  = Child();
        child.confidence.confidence = confidence;
        std::vector<AiDetectRstEl> parents{Parent()};
        AssociateTargets(parents, {child, Child("face")}, PlateConfig());
        REQUIRE(parents[0].association_status == TargetAssociationStatus::kMissing);
    }
}

TEST_CASE("Legacy face settings and generic settings preserve their intended defaults", "[association]") {
    TargetAssociationConfig config;
    REQUIRE(UpdateTargetAssociationConfig(
        config, {Param("param.minFaceSize", "80"), Param("param.faceDetectionConfidence", "0.7")}));
    REQUIRE(config.IsFaceAssociation());
    REQUIRE(config.region == AssociationRegion::kUpperHalf);
    REQUIRE(config.min_size == 80);
    REQUIRE(config.confidence == Catch::Approx(0.7f));
    REQUIRE(UpdateTargetAssociationConfig(
        config, {Param("param.minFaceSize", "90"), Param("param.minTargetSize", "20"),
                 Param("param.associationLabels", "[\"plate\",\"plate\"]"),
                 Param("param.associationRegion", "lower"), Param("param.minContainment", "0.8")}));
    REQUIRE(config.min_size == 20);
    REQUIRE(config.labels == std::vector<std::string>{"plate"});
    REQUIRE(config.region == AssociationRegion::kLowerHalf);
    REQUIRE(config.min_containment == Catch::Approx(0.8));
    REQUIRE_FALSE(config.IsFaceAssociation());
    for (const auto& param :
         {Param("param.associationLabels", "face"), Param("param.associationLabels", "[1]"),
          Param("param.associationLabels", "[\"\"]"), Param("param.minTargetSize", "0"),
          Param("param.minContainment", "nan"), Param("param.detectionConfidence", "1.1"),
          Param("param.associationRegion", "unknown")}) {
        REQUIRE_FALSE(UpdateTargetAssociationConfig(config, {Param("param.minTargetSize", "50"), param}));
        REQUIRE(config.min_size == 20);
        REQUIRE(config.labels == std::vector<std::string>{"plate"});
    }
}

TEST_CASE("Generic face association adapts to the existing stranger evidence contract",
          "[association][stranger]") {
    std::vector<AiDetectRstEl> generic{Parent()}, legacy{Parent()};
    auto face = Child("face", 40, 10);
    AssociateTargets(generic, {face}, TargetAssociationConfig{});
    UpdateAssociatedFaceObservations(generic);
    AssociatePersonFaces(legacy, {face}, 60);
    REQUIRE(generic[0].relatedEl.bActive == legacy[0].relatedEl.bActive);
    REQUIRE(generic[0].face_observation.status == legacy[0].face_observation.status);
    REQUIRE(generic[0].face_observation.status == FaceObservationStatus::kNotObserved);
    std::vector<AiDetectRstEl> unavailable{Parent()};
    AssociateTargets(unavailable, {}, TargetAssociationConfig{}, false);
    UpdateAssociatedFaceObservations(unavailable);
    REQUIRE(unavailable[0].face_observation.status == FaceObservationStatus::kUnavailable);
}
