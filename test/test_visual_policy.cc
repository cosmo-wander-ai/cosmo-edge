#include <cmath>
#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "flow/alarm/AlarmVisualState.h"
#include "service/ai/impl/VisualDecisionServiceImpl.h"
#include "util/UuidUtil.h"

namespace {
using namespace cosmo;
using namespace cosmo::service;
using Json = nlohmann::json;
using namespace std::chrono_literals;

VisualDecisionRequest PolicyRequest() {
    VisualDecisionRequest result{"frame", "roi", {}, "filter", "entrance", std::string(64, 'f')};
    for (const auto& id : {"helmet", "person"})
        result.questions.push_back({id,
                                    id,
                                    1,
                                    std::string(64, std::string(id) == "helmet" ? 'e' : 'd'),
                                    2,
                                    {"false", "true"},
                                    {{"value", 1.0}, {"bucket", "image|noul:2"}, {"source", "test"}}});
    return result;
}
Json Acceptance(const std::string& aggregate = "all", const std::string& unknown = "keep") {
    Json rules = Json::array();
    for (const auto& q : PolicyRequest().questions)
        rules.push_back({{"compiled_sha256", q.compiledSha256},
                         {"positive_options", {"true"}},
                         {"min_probability", 0.8},
                         {"min_margin", 0.2}});
    return {{"schema", 1},
            {"manifest_sha256", std::string(64, 'a')},
            {"engine_sha256", std::string(64, 'b')},
            {"acceptance_sha256", std::string(64, 'c')},
            {"profiles",
             {{{"id", "entrance"},
               {"status", "accepted"},
               {"task_ids", {"camera_15"}},
               {"configuration_sha256", std::string(64, 'f')},
               {"evidence_ref", "fixture-acceptance"},
               {"aggregation", aggregate},
               {"unknown", unknown},
               {"questions", rules}}}}};
}
Json Prediction(bool positive, bool ambiguous = false) {
    Json result{{"manifest_sha256", std::string(64, 'a')}, {"status", "completed"}, {"items", Json::array()}};
    for (const auto& q : PolicyRequest().questions) {
        auto item      = visual::ItemIdentity(q);
        const double p = ambiguous ? 0.55 : 0.9;
        item.update({{"status", "completed"},
                     {"top1", positive ? "true" : "false"},
                     {"probabilities", positive ? Json{1 - p, p} : Json{p, 1 - p}},
                     {"top2_margin", 2 * p - 1},
                     {"business_qualified", false}});
        result["items"].push_back(item);
    }
    return result;
}
class Audit final : public IVisualAuditService {
public:
    bool fail{false};
    Json stored;
    VisualAuditReceipt Begin(const Json&) noexcept override {
        return {{{"begin", "stored"}}, {}};
    }
    bool Complete(VisualAuditReceipt& receipt, const Json& value) noexcept override {
        if (!fail)
            stored = value;
        receipt.metadata["finish"] = fail ? "write_failed" : "stored";
        return !fail;
    }
    Json Page(const std::string&, const std::string&, int, int) override {
        return {};
    }
    Json Status() const override {
        return {{"available", true}};
    }
};
}  // namespace

TEST_CASE("Visual acceptance binds engine model tasks geometry and exact question packs", "[visual-policy]") {
    auto accepted = Acceptance();
    // Use valid hexadecimal compiled pack identities.
    auto request = PolicyRequest();
    for (size_t i = 0; i < request.questions.size(); ++i) {
        request.questions[i].compiledSha256                        = std::string(64, i ? 'd' : 'e');
        accepted["profiles"][0]["questions"][i]["compiled_sha256"] = request.questions[i].compiledSha256;
    }
    REQUIRE_NOTHROW(visual::ValidateAcceptance(accepted, std::string(64, 'a'), std::string(64, 'b')));
    CHECK_THROWS(visual::ValidateAcceptance(accepted, std::string(64, '0'), std::string(64, 'b')));
    CHECK_THROWS(visual::ValidateAcceptance(accepted, std::string(64, 'a'), std::string(64, '0')));
    auto response = Prediction(false);
    for (size_t i = 0; i < request.questions.size(); ++i)
        response["items"][i]["compiled_sha256"] = request.questions[i].compiledSha256;
    REQUIRE(visual::EvaluateDecision(accepted, request, "camera_15", response)["filter_applied"] == true);
    SECTION("different task is unqualified") {
        CHECK(visual::EvaluateDecision(accepted, request, "camera_99", response)["retain"] == true);
    }
    SECTION("edited regions are unqualified") {
        request.qualificationRevision = std::string(64, '0');
        CHECK(visual::EvaluateDecision(accepted, request, "camera_15", response)["business_qualified"] ==
              false);
    }
    SECTION("edited question is unqualified") {
        request.questions[0].compiledSha256 = std::string(64, '0');
        CHECK(visual::EvaluateDecision(accepted, request, "camera_15", response)["business_qualified"] ==
              false);
    }
    SECTION("review never suppresses even accepted negative predictions") {
        request.mode = "review";
        CHECK(visual::EvaluateDecision(accepted, request, "camera_15", response)["retain"] == true);
    }
    SECTION("missing acceptance preserves original alarm") {
        CHECK(visual::EvaluateDecision({}, request, "camera_15", response)["retain"] == true);
    }
}

TEST_CASE("Visual aggregation and low confidence rules are independent of worker qualification claims",
          "[visual-policy]") {
    const auto mode      = GENERATE(std::string("all"), std::string("any"));
    auto request         = PolicyRequest();
    auto response        = Prediction(false);
    response["items"][1] = Prediction(true)["items"][1];
    auto decision        = visual::EvaluateDecision(Acceptance(mode), request, "camera_15", response);
    CHECK(decision["retain"] == (mode == "any"));
    CHECK(response["items"][0]["business_qualified"] == false);
    CHECK(visual::EvaluateDecision(Acceptance(mode, "keep"), request, "camera_15",
                                   Prediction(false, true))["retain"] == true);
    CHECK(visual::EvaluateDecision(Acceptance(mode, "drop"), request, "camera_15",
                                   Prediction(false, true))["retain"] == false);
    response["status"] = "unknown";
    response["reason"] = "worker_unavailable";
    CHECK(visual::EvaluateDecision(Acceptance(mode, "drop"), request, "camera_15",
                                   response)["filter_applied"] == false);
}

TEST_CASE("Visual filtering decision is persisted before it can suppress a candidate", "[visual-policy]") {
    Audit audit;
    audit.fail = GENERATE(false, true);
    VisualDecisionOptions options;
    options.release    = {std::string(64, 'a'),
                          {{"tower", std::string(64, 'b')},
                           {"adapter", std::string(64, 'c')},
                           {"decision", std::string(64, 'd')}}};
    options.acceptance = Acceptance();
    auto request       = PolicyRequest();
    for (size_t i = 0; i < request.questions.size(); ++i) {
        request.questions[i].compiledSha256 = std::string(64, i ? 'd' : 'e');
        options.acceptance["profiles"][0]["questions"][i]["compiled_sha256"] =
            request.questions[i].compiledSha256;
    }
    VisualDecisionServiceImpl service(
        options,
        [&](const auto&, const Json& wire, const auto& bytes, auto) {
            auto reply = visual::Failure(wire, "unused");
            reply.erase("reason");
            reply["status"]       = "completed";
            reply["model_hashes"] = options.release.modelHashes;
            reply["image_sha256"] = visual::Sha256(bytes.data(), bytes.size());
            for (auto& row : reply["items"]) {
                row.erase("reason");
                row.update({{"status", "completed"},
                            {"ordered_options", {"false", "true"}},
                            {"qtype", 2},
                            {"temperature", request.questions[0].temperature},
                            {"probabilities", {0.9, 0.1}},
                            {"raw_option_logits", {std::log(.9), std::log(.1)}},
                            {"raw_action_logits", {0., 1.}},
                            {"top1", "false"},
                            {"top2_margin", 0.8},
                            {"decision_timing_ms",
                             {{"h2d_ms", 0.}, {"launch_sync_ms", 0.}, {"d2h_ms", 0.}, {"total_ms", 0.}}}});
            }
            return reply;
        },
        &audit);
    auto result = service.Decide(
        request, std::make_shared<VisualDecisionRun>("camera_15", "run", "config"),
        [] { return VisualDecisionImage{{1, 2, 3}, 32, 32}; }, 1s);
    REQUIRE(result.AllCompleted());
    CHECK(result.Retain() == audit.fail);
    if (!audit.fail)
        CHECK(audit.stored == result.response);
    else
        CHECK(result.response["decision"]["reason"] == "audit_result_write_failed");
}

TEST_CASE("Mixed target alarm removes rejected target and preserves retained overlay identity",
          "[visual-policy]") {
    DataAlarmUnit unit;
    for (int i = 0; i < 2; ++i) {
        AlarmVisualCandidate candidate;
        candidate.box     = {i * 100, 10, 30, 40};
        candidate.trackId = std::to_string(i);
        unit.visualCandidates.push_back(candidate);
        CMsgOnEventsTarget target;
        target.trackId    = candidate.trackId;
        target.box.x      = candidate.box.x;
        target.box.y      = 10;
        target.box.width  = 30;
        target.box.height = 40;
        unit.targets.push_back(target);
        unit.boxs.emplace_back(candidate.box);
    }
    CHECK_FALSE(alarm::RetainVisualTargets(unit, {false, false}));
    REQUIRE(alarm::RetainVisualTargets(unit, {false, true}));
    REQUIRE(unit.targets.size() == 1);
    CHECK(unit.targets[0].trackId == "1");
    REQUIRE(unit.boxs.size() == 1);
    CHECK(unit.boxs[0].box.x == 100);
    CHECK(unit.box.x == 100);
}
