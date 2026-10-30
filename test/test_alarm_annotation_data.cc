#include <limits>

#include "catch_amalgamated.hpp"
#include "flow/alarm/AlarmBatch.h"
#include "flow/alarm/AlarmVisualState.h"
#include "flow/logical/LogicCalcEngine.h"

using namespace cosmo;

namespace {
AiDetectRstEl Detection(std::string id, std::string label, float score) {
    AiDetectRstEl result;
    result.targetId   = std::move(id);
    result.box        = {10, 20, 30, 40};
    result.confidence = {std::move(label), "detector", score, AiAnnotationSource::Detection};
    return result;
}

LogicCalc PositiveRule(std::string label) {
    LogicCalc rule;
    rule.type         = LogicType::GE;
    rule.keyLElements = {"aiOut", std::move(label), "confidence"};
    rule.keyRElements = {"aiParam", "threshold", "confidence"};
    return rule;
}

LogicCalcEngine Engine() {
    return {"annotation-test",
            [](const std::string&, float& value) {
                value = 0.5F;
                return true;
            },
            [](const std::string&, const std::string&) { return false; }};
}

DataAlarmUnit Alarm(const AiDetectRstEl& target) {
    DataAlarmUnit unit;
    unit.box = target.box;
    unit.boxs.push_back(MakeAlarmBox(target));
    unit.targets.push_back(MakeOnEventsTarget(target));
    AlarmVisualCandidate candidate;
    candidate.box            = target.box;
    candidate.trackId        = target.trackIdInfo;
    candidate.sourceTargetId = target.targetId;
    unit.visualCandidates.push_back(candidate);
    return unit;
}
}  // namespace

TEST_CASE("alarm annotations require measured provenance, not a plausible score", "[alarm][annotation]") {
    auto target  = Detection("target", "fire", 0.83F);
    auto overlay = MakeAlarmBox(target);
    REQUIRE(overlay.annotations.size() == 1);
    CHECK(overlay.annotations[0].label == "fire");
    CHECK(overlay.annotations[0].atomic_code == "detector");
    CHECK(overlay.annotations[0].score == 0.83F);
    CHECK(overlay.sourceTargetId == "target");

    target.confidence = {"user VLM prompt", "", 1.0F};
    CHECK(MakeAlarmBox(target).annotations.empty());
    target.confidence  = {"", "classifier", 1.0F};
    target.classifyRst = {{"no_helmet", "classifier", 0.93F, AiAnnotationSource::Classification}};
    CHECK(MakeAlarmBox(target).annotations.empty());  // No rule selected this class.

    target  = Detection("target", "fire", std::numeric_limits<float>::quiet_NaN());
    overlay = MakeAlarmBox(target);
    REQUIRE(overlay.annotations.size() == 1);
    CHECK_FALSE(overlay.annotations[0].score.has_value());
    target.confidence.confidence = 8.0F;
    CHECK_FALSE(MakeAlarmBox(target).annotations[0].score.has_value());
}

TEST_CASE("alarm classification annotation follows the matched class, not top one", "[alarm][annotation]") {
    auto target        = Detection("person", "person", 0.98F);
    target.classifyRst = {{"unrelated_top_class", "ppe", 0.99F, AiAnnotationSource::Classification},
                          {"no_helmet", "ppe", 0.82F, AiAnnotationSource::Classification}};
    auto rule          = PositiveRule("no_helmet");
    auto engine        = Engine();
    CHECK(engine.ApplyLogicResult(target, rule) == engine.GetLogicResult(target, rule));
    auto overlay = MakeAlarmBox(target);
    REQUIRE(overlay.annotations.size() == 1);
    CHECK(overlay.annotations[0].label == "no_helmet");
    CHECK(overlay.annotations[0].atomic_code == "ppe");
    CHECK(overlay.annotations[0].score == 0.82F);
    CHECK(overlay.annotations[0].source == AiAnnotationSource::Classification);

    target.classifyRst[1].confidence = 0.12F;
    CHECK_FALSE(engine.ApplyLogicResult(target, rule));
    CHECK(target.alarmAnnotations.empty());
    CHECK(MakeAlarmBox(target).annotations[0].label == "person");
}

TEST_CASE("rule evidence respects failed branches and existing short circuit behavior",
          "[alarm][annotation]") {
    auto target        = Detection("person", "person", 0.98F);
    target.classifyRst = {{"A", "classifier", 0.9F, AiAnnotationSource::Classification},
                          {"B", "classifier", 0.1F, AiAnnotationSource::Classification},
                          {"C", "classifier", 0.8F, AiAnnotationSource::Classification}};
    LogicCalc failed;
    failed.type = LogicType::AND;
    failed.list = {PositiveRule("A"), PositiveRule("B")};
    auto engine = Engine();
    CHECK_FALSE(engine.ApplyLogicResult(target, failed));
    CHECK(target.alarmAnnotations.empty());
    LogicCalc success;
    success.type = LogicType::OR;
    success.list = {failed, PositiveRule("C"), PositiveRule("A")};
    CHECK(engine.ApplyLogicResult(target, success));
    REQUIRE(target.alarmAnnotations.size() == 1);
    CHECK(target.alarmAnnotations[0].label == "C");

    auto absent = PositiveRule("B");
    absent.type = LogicType::Less;
    CHECK(engine.ApplyLogicResult(target, absent));
    CHECK(target.alarmAnnotations.empty());  // Neither an opposite label nor 1-0.1 is invented.

    auto reversed = PositiveRule("C");
    std::swap(reversed.keyLElements, reversed.keyRElements);
    reversed.type = LogicType::LE;
    CHECK(engine.ApplyLogicResult(target, reversed));
    REQUIRE(target.alarmAnnotations.size() == 1);
    CHECK(target.alarmAnnotations[0].score == 0.8F);
}

TEST_CASE("positive attribute equality retains its measured class and never infers a negative label",
          "[alarm][annotation]") {
    auto target = Detection("person", "person", 0.97F);
    target.attrRst.push_back({"clothing", "no_uniform", "uniform-classifier", 0.88F,
                              AiAnnotationSource::Classification, "uniform-measurement"});
    LogicCalc rule;
    rule.type         = LogicType::Equal;
    rule.keyLElements = {"aiOut", "attribute", "clothing"};
    rule.keyRElements = {"aiParam", "attribute", "no_uniform"};
    auto engine       = Engine();
    REQUIRE(engine.ApplyLogicResult(target, rule));
    REQUIRE(target.alarmAnnotations.size() == 1);
    CHECK(target.alarmAnnotations[0].label == "no_uniform");
    CHECK(target.alarmAnnotations[0].score == 0.88F);
    rule.type            = LogicType::NEQ;
    rule.keyRElements[2] = "uniform";
    REQUIRE(engine.ApplyLogicResult(target, rule));
    CHECK(target.alarmAnnotations.empty());
}

TEST_CASE("copied classification evidence remains attached to its exact related source",
          "[alarm][annotation]") {
    auto target = Detection("body", "person", 0.94F);
    AiConfidence measured{"no_helmet", "head-classifier", 0.86F, AiAnnotationSource::Classification,
                          "measurement-head-1"};
    target.classifyRst.push_back(measured);
    target.relatedEl.bActive    = true;
    target.relatedEl.box        = {12, 20, 10, 12};
    target.relatedEl.confidence = {"head", "head-detector", 0.75F, AiAnnotationSource::Detection};
    target.relatedEl.classifyRst.push_back(measured);
    AiDetectRelatedEl other            = target.relatedEl;
    other.classifyRst[0].measurementId = "measurement-head-2";
    target.relatedEls.push_back(other);  // Same geometry/class/score is still a different result.
    auto rule = PositiveRule("no_helmet");
    REQUIRE(Engine().ApplyLogicResult(target, rule));
    CHECK(MakeAlarmBox(target).annotations[0].label == "person");
    CHECK(MakeAlarmBox(target.relatedEl).annotations[0].label == "no_helmet");
    CHECK(MakeAlarmBox(target.relatedEls[0]).annotations[0].label == "head");
    CHECK(MakeAlarmBox(target.relatedEl).annotations[0].score == 0.86F);
}

TEST_CASE("batched visual filtering preserves annotations for coincident distinct targets",
          "[alarm][annotation]") {
    auto first  = Detection("A", "fire", 0.81F);
    auto second = Detection("B", "smoke", 0.92F);
    std::deque<DataAlarmUnit> units{Alarm(first), Alarm(second)};
    auto merged = alarm::MergeAlarmBatch(units, {1, 0});
    REQUIRE(merged.boxs.size() == 2);
    CHECK(merged.boxs[0].annotations[0].label == "smoke");
    CHECK(merged.boxs[1].annotations[0].label == "fire");
    std::swap(merged.boxs[0], merged.boxs[1]);  // Mapping must use identity, not array position.
    REQUIRE(alarm::RetainVisualTargets(merged, {true, false}));
    REQUIRE(merged.boxs.size() == 1);
    CHECK(merged.boxs[0].sourceTargetId == "B");
    CHECK(merged.boxs[0].annotations[0].label == "smoke");
    CHECK(merged.boxs[0].annotations[0].score == 0.92F);
}

TEST_CASE("visual filtering keeps its decisions when annotation correspondence is unavailable",
          "[alarm][annotation]") {
    auto first  = Detection("", "fire", 0.81F);
    auto second = Detection("", "smoke", 0.92F);
    SECTION("unique geometry preserves legacy filtering") {
        second.box.x = 100;
        auto merged  = alarm::MergeAlarmBatch({Alarm(first), Alarm(second)}, {0, 1});
        REQUIRE(alarm::RetainVisualTargets(merged, {false, true}));
        REQUIRE(merged.boxs.size() == 1);
        CHECK(merged.boxs[0].annotations[0].label == "smoke");
    }
    SECTION("identical geometry without identity filters but does not guess annotations") {
        auto merged = alarm::MergeAlarmBatch({Alarm(first), Alarm(second)}, {0, 1});
        REQUIRE(alarm::RetainVisualTargets(merged, {false, true}));
        REQUIRE(merged.boxs.size() == 1);
        CHECK(merged.boxs[0].annotations.empty());
        CHECK(merged.targets.size() == 1);
    }
    SECTION("extra related overlay does not prevent the original target filtering") {
        auto merged = alarm::MergeAlarmBatch({Alarm(first), Alarm(second)}, {0, 1});
        merged.boxs.emplace_back(util::Box{5, 6, 7, 8});
        REQUIRE(alarm::RetainVisualTargets(merged, {false, true}));
        CHECK(merged.boxs.size() == 1);
        CHECK(merged.targets.size() == 1);
    }
}
