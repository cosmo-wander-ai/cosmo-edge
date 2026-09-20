#include <limits>

#include "catch_amalgamated.hpp"
#include "flow/logical/LogicCalcEngine.h"

using namespace cosmo;

namespace {
LogicCalc HelmetRule() {
    LogicCalc rule;
    rule.type         = LogicType::Greater;
    rule.keyLElements = {"aiOut", "helmet", "confidence"};
    rule.keyRElements = {"aiParam", "helmet", "confidence"};
    return rule;
}
AiDetectRstEl HelmetTarget(float score) {
    AiDetectRstEl target;
    target.confidence.label      = "helmet";
    target.confidence.confidence = score;
    return target;
}
}  // namespace

TEST_CASE("Logic observations distinguish false from missing operands", "[LogicCalcEngine][accumulation]") {
    bool configured = true;
    float threshold = 0.5f;
    LogicCalcEngine engine(
        "test",
        [&](const std::string&, float& value) {
            value = threshold;
            return configured;
        },
        [](const std::string&, const std::string&) { return false; });
    auto rule   = HelmetRule();
    auto target = HelmetTarget(0.2f);
    REQUIRE(engine.Observe(target, rule) == std::optional<bool>{false});
    target.confidence.confidence = 0.9f;
    REQUIRE(engine.Observe(target, rule) == std::optional<bool>{true});
    target.confidence.label = "person";
    REQUIRE_FALSE(engine.Observe(target, rule).has_value());
    target.confidence.label = "helmet";
    configured              = false;
    REQUIRE_FALSE(engine.Observe(target, rule).has_value());
    configured = true;
    threshold  = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(engine.Observe(target, rule).has_value());
}

TEST_CASE("Compound observations retain decisive results and unknown operands",
          "[LogicCalcEngine][accumulation]") {
    LogicCalcEngine engine(
        "test",
        [](const std::string&, float& value) {
            value = 0.5f;
            return true;
        },
        [](const std::string&, const std::string&) { return false; });
    auto known              = HelmetRule();
    auto missing            = known;
    missing.keyLElements[1] = "missing";
    LogicCalc rule;
    rule.type   = LogicType::OR;
    rule.list   = {missing, known};
    auto target = HelmetTarget(0.9f);
    REQUIRE(engine.Observe(target, rule) == std::optional<bool>{true});
    target.confidence.confidence = 0.2f;
    REQUIRE_FALSE(engine.Observe(target, rule).has_value());
    rule.type = LogicType::AND;
    REQUIRE(engine.Observe(target, rule) == std::optional<bool>{false});
    target.confidence.confidence = 0.9f;
    REQUIRE_FALSE(engine.Observe(target, rule).has_value());
    rule.type = LogicType::NOR;
    REQUIRE_FALSE(engine.Observe(target, rule).has_value());
}

TEST_CASE("Membership observations require a configured selection", "[LogicCalcEngine][accumulation]") {
    LogicCalcEngine engine(
        "test", [](const std::string&, float&) { return false; },
        [](const std::string&, const std::string&) { return false; });
    LogicCalc rule;
    rule.type = LogicType::NonInclude;
    rule.keyL = "custom.detectLabels";
    rule.keyR = "helmet";
    REQUIRE_FALSE(engine.Observe({}, rule).has_value());
    REQUIRE_FALSE(engine.Observe({}, rule, [](const std::string&) { return false; }).has_value());
    REQUIRE(engine.Observe({}, rule, [](const std::string&) { return true; }) == std::optional<bool>{true});
}
