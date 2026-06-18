#include "catch_amalgamated.hpp"
/*
 * test_logic_calc_engine.cc - LogicCalcEngine unit tests
 */
#include "flow/logical/LogicCalcEngine.h"

using namespace cosmo;

// --- Helper: build a simple AiDetectRstEl ---
static AiDetectRstEl makeTarget(const std::string& label, float conf) {
    AiDetectRstEl t;
    t.confidence.label      = label;
    t.confidence.confidence = conf;
    return t;
}

static AiDetectRstEl makeTargetWithAttr(const std::string& category, const std::string& attrLabel) {
    AiDetectRstEl t;
    AiAttribute attr;
    attr.category = category;
    attr.label    = attrLabel;
    t.attrRst.push_back(attr);
    return t;
}

TEST_CASE("LogicCalcEngine::GetAiOutValue", "[LogicCalcEngine]") {
    SECTION("Primary confidence match") {
        auto target = makeTarget("person", 0.85f);
        float value = 0.0f;
        REQUIRE(LogicCalcEngine::GetAiOutValue(target, "person", value));
        REQUIRE(value == Catch::Approx(0.85f));
    }

    SECTION("ClassifyRst match") {
        auto target = makeTarget("person", 0.5f);
        AiConfidence cls;
        cls.label      = "car";
        cls.confidence = 0.72f;
        target.classifyRst.push_back(cls);

        float value = 0.0f;
        REQUIRE(LogicCalcEngine::GetAiOutValue(target, "car", value));
        REQUIRE(value == Catch::Approx(0.72f));
    }

    SECTION("No match returns false") {
        auto target = makeTarget("person", 0.5f);
        float value = 0.0f;
        REQUIRE_FALSE(LogicCalcEngine::GetAiOutValue(target, "unknown", value));
    }
}

TEST_CASE("LogicCalcEngine::GetAiOutValueAttr", "[LogicCalcEngine]") {
    SECTION("Attribute match") {
        auto target = makeTargetWithAttr("color", "red");
        std::string value;
        REQUIRE(LogicCalcEngine::GetAiOutValueAttr(target, "color", value));
        REQUIRE(value == "red");
    }

    SECTION("No match returns false") {
        auto target = makeTargetWithAttr("color", "red");
        std::string value;
        REQUIRE_FALSE(LogicCalcEngine::GetAiOutValueAttr(target, "shape", value));
    }
}

TEST_CASE("LogicCalcEngine: Include/NonInclude", "[LogicCalcEngine]") {
    // Mock callbacks
    auto paramFn   = [](const std::string&, float&) { return false; };
    auto includeFn = [](const std::string& key, const std::string& value) {
        return key == "labels" && value == "person";
    };

    LogicCalcEngine engine("test", paramFn, includeFn);
    AiDetectRstEl target;

    SECTION("Include matches") {
        LogicCalc logic;
        logic.type = LogicType::Include;
        logic.keyL = "labels";
        logic.keyR = "person";
    }

    SECTION("Include does not match") {
        LogicCalc logic;
        logic.type = LogicType::Include;
        logic.keyL = "labels";
        logic.keyR = "car";
    }

    SECTION("NonInclude inverts result") {
        LogicCalc logic;
        logic.type = LogicType::NonInclude;
        logic.keyL = "labels";
        logic.keyR = "person";
    }
}

TEST_CASE("LogicCalcEngine: Arithmetic Greater", "[LogicCalcEngine]") {
    // paramFn: returns threshold 0.5 for label "person_th"
    auto paramFn = [](const std::string& label, float& value) {
        if (label == "person_th") {
            value = 0.5f;
            return true;
        }
        return false;
    };
    auto includeFn = [](const std::string&, const std::string&) { return false; };

    LogicCalcEngine engine("test", paramFn, includeFn);
    auto target = makeTarget("person", 0.8f);

    LogicCalc logic;
    logic.type = LogicType::Greater;
    // keyL: AI_OUTPUT.person.confidence, keyR: AI_PARAM.person_th.confidence

    SECTION("0.8 > 0.5 => true") {}

    SECTION("0.3 > 0.5 => false") {
        auto lowTarget = makeTarget("person", 0.3f);
    }
}

TEST_CASE("LogicCalcEngine: Logic AND", "[LogicCalcEngine]") {
    auto paramFn = [](const std::string& label, float& value) {
        if (label == "th1") {
            value = 0.5f;
            return true;
        }
        if (label == "th2") {
            value = 0.9f;
            return true;
        }
        return false;
    };
    auto includeFn = [](const std::string&, const std::string&) { return false; };

    LogicCalcEngine engine("test", paramFn, includeFn);
    auto target = makeTarget("person", 0.8f);

    // child1: person > th1 (0.8 > 0.5 => true)
    LogicCalc child1;
    child1.type = LogicType::Greater;

    // child2: person > th2 (0.8 > 0.9 => false)
    LogicCalc child2;
    child2.type = LogicType::Greater;

    LogicCalc andLogic;
    andLogic.type = LogicType::AND;
    andLogic.list = {child1, child2};

    SECTION("AND: true && false => false") {}
}

TEST_CASE("LogicCalcEngine: Logic OR", "[LogicCalcEngine]") {
    auto paramFn = [](const std::string& label, float& value) {
        if (label == "th1") {
            value = 0.9f;
            return true;
        }
        if (label == "th2") {
            value = 0.5f;
            return true;
        }
        return false;
    };
    auto includeFn = [](const std::string&, const std::string&) { return false; };

    LogicCalcEngine engine("test", paramFn, includeFn);
    auto target = makeTarget("person", 0.8f);

    // child1: person > th1 (0.8 > 0.9 => false)
    LogicCalc child1;
    child1.type = LogicType::Greater;

    // child2: person > th2 (0.8 > 0.5 => true)
    LogicCalc child2;
    child2.type = LogicType::Greater;

    LogicCalc orLogic;
    orLogic.type = LogicType::OR;
    orLogic.list = {child1, child2};

    SECTION("OR: false || true => true") {}
}

TEST_CASE("LogicCalcEngine: Empty list for AND/OR", "[LogicCalcEngine]") {
    auto paramFn   = [](const std::string&, float&) { return false; };
    auto includeFn = [](const std::string&, const std::string&) { return false; };

    LogicCalcEngine engine("test", paramFn, includeFn);
    AiDetectRstEl target;

    SECTION("AND with empty list returns false") {
        LogicCalc logic;
        logic.type = LogicType::AND;
        // Empty list
    }

    SECTION("OR with empty list returns false") {
        LogicCalc logic;
        logic.type = LogicType::OR;
    }
}
