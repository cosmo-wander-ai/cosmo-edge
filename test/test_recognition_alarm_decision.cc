#include <nlohmann/json.hpp>
#include <unordered_set>

#include "catch_amalgamated.hpp"
#include "flow/recognizer/RecognitionAlarmDecision.h"
#include "util/dto/ClientMsgEvent.h"

TEST_CASE("recognition alarm decision preserves known-person mode", "[recognition][alarm-decision]") {
    cosmo::RecognitionAlarmDecision decision;
    decision.SetConfig({cosmo::MatchFlagType::Match, 3});

    CHECK(decision.ShouldAlarm(10, true, true));
    CHECK_FALSE(decision.ShouldAlarm(11, false, true));
    CHECK_FALSE(decision.ShouldAlarm(12, true, false));
}

TEST_CASE("stranger alarm requires consecutive non-matches", "[recognition][alarm-decision]") {
    cosmo::RecognitionAlarmDecision decision;
    decision.SetConfig({cosmo::MatchFlagType::NotMatch, 3});

    CHECK_FALSE(decision.ShouldAlarm(20, false, true));
    CHECK_FALSE(decision.ShouldAlarm(20, false, true));
    CHECK(decision.ShouldAlarm(20, false, true));
    CHECK(decision.ShouldAlarm(20, false, true));
}

TEST_CASE("a known match suppresses stranger alarms for the active track", "[recognition][alarm-decision]") {
    cosmo::RecognitionAlarmDecision decision;
    decision.SetConfig({cosmo::MatchFlagType::NotMatch, 3});

    CHECK_FALSE(decision.ShouldAlarm(30, false, true));
    CHECK_FALSE(decision.ShouldAlarm(30, true, true));
    CHECK_FALSE(decision.ShouldAlarm(30, false, true));
    CHECK_FALSE(decision.ShouldAlarm(30, false, true));
    CHECK_FALSE(decision.ShouldAlarm(30, false, true));

    decision.RetainTracks(std::unordered_set<int>{});
    CHECK_FALSE(decision.ShouldAlarm(30, false, true));
    CHECK_FALSE(decision.ShouldAlarm(30, false, true));
    CHECK(decision.ShouldAlarm(30, false, true));
}

TEST_CASE("stranger alarm ignores unavailable comparison libraries", "[recognition][alarm-decision]") {
    cosmo::RecognitionAlarmDecision decision;
    decision.SetConfig({cosmo::MatchFlagType::NotMatch, 1});

    CHECK_FALSE(decision.ShouldAlarm(40, false, false));
}

TEST_CASE("recognition event carries explicit stranger state with legacy fallback",
          "[recognition][event-json]") {
    cosmo::CMsgOnEventsPropertyRecognition stranger;
    stranger.matched     = 0;
    stranger.matchDegree = 71.5f;

    nlohmann::json encoded = stranger;
    CHECK(encoded.at("matched") == 0);
    CHECK(encoded.at("matchName") == "");
    CHECK(encoded.at("personId") == "");
    CHECK(encoded.at("LibImage") == "");

    auto decoded = encoded.get<cosmo::CMsgOnEventsPropertyRecognition>();
    CHECK(decoded.matched == 0);
    CHECK(decoded.matchDegree == Catch::Approx(71.5f));

    nlohmann::json legacy = {{"matchDegree", 88.0f}};
    auto legacy_decoded   = legacy.get<cosmo::CMsgOnEventsPropertyRecognition>();
    CHECK(legacy_decoded.matched == -1);
}
