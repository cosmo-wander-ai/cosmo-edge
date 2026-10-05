#include <filesystem>
#include <thread>

#include "catch_amalgamated.hpp"
#include "flow/alarm/LayaReview.h"
#include "util/UuidUtil.h"
using namespace std::chrono_literals;
namespace {
using Json = nlohmann::json;
struct StoreFixture {
    std::string path =
        (std::filesystem::temp_directory_path() / (cosmo::util::GenerateUUID() + ".db")).string();
    cosmo::LayaReviewStore store{path};
    ~StoreFixture() {
        for (const auto& suffix : {"", "-wal", "-shm"})
            std::filesystem::remove(path + suffix);
    }
};
Json Identity(const std::string& id = "event-1") {
    return {{"event_id", id},           {"request_id", id},       {"created_ms", int64_t(123)},
            {"task_id", "helmet-task"}, {"run_epoch", "epoch-1"}, {"publication", "pending"}};
}
Json Response(const Json& request) {
    return {{"protocol", "laya-shadow-v1"},
            {"profile", "laya-256p-256s-v1"},
            {"request_id", request.at("request_id")},
            {"run_epoch", request.at("run_epoch")},
            {"question_id", "helmet-review-en-v1"},
            {"question_version", 1},
            {"decision_status", "unknown"},
            {"reason", "qualification_pending"},
            {"ordered_options", {"wearing", "not_wearing", "uncertain"}},
            {"probabilities", {0.995, 0.004, 0.001}},
            {"model_hashes",
             {{"tower", "41c82b46528e171686faac6b76c408b31abba7d7986ac8c799d600e9ee190aff"},
              {"adapter", "d948ff1fecaf790cd17b327ae99bf2afa42dc53d9ed958a317ff6e33fe1c39e9"},
              {"decision", "4bf88150317d05e23e7c29e831287684f702a829d4d0975a5ef1811a6417c19e"}}}};
}
auto Image = [] { return cosmo::LayaShadowPayload{{{"image_width", 1}, {"image_height", 1}}, {1, 2, 3}}; };
}  // namespace
TEST_CASE("Laya policy requires qualification and pinned identities", "[laya][review]") {
    auto request  = Identity();
    auto response = Response(request);
    REQUIRE(cosmo::LayaReview::Classify(request, response, false)["decision"] == "unknown");
    REQUIRE(cosmo::LayaReview::Classify(request, response, true)["decision"] == "reject");
    response["probabilities"] = {0.004, 0.995, 0.001};
    REQUIRE(cosmo::LayaReview::Classify(request, response, true)["decision"] == "confirm");
    response["probabilities"] = {0.2, 0.2, 0.6};
    REQUIRE(cosmo::LayaReview::Classify(request, response, true)["decision"] == "unknown");
    response["probabilities"]         = {0.995, 0.004, 0.001};
    response["model_hashes"]["tower"] = "other";
    REQUIRE(cosmo::LayaReview::Classify(request, response, true)["reason"] == "model_identity_mismatch");
    response              = Response(request);
    response["run_epoch"] = "previous-run";
    REQUIRE(cosmo::LayaReview::Classify(request, response, true)["decision"] == "unknown");
    response                  = Response(request);
    response["probabilities"] = {2, -1, 0};
    REQUIRE(cosmo::LayaReview::Classify(request, response, true)["decision"] == "unknown");
}
TEST_CASE("Laya audit completion and publication are immutable", "[laya][review]") {
    StoreFixture f;
    auto r = Identity();
    REQUIRE(f.store.Begin(r));
    REQUIRE_FALSE(f.store.Begin(r));
    REQUIRE(f.store.Finish("event-1", {{"decision", "unknown"}, {"reason", "deadline_exceeded"}}));
    REQUIRE_FALSE(f.store.Finish("event-1", {{"decision", "reject"}}));
    REQUIRE(f.store.Publication("event-1", "published"));
    REQUIRE_FALSE(f.store.Publication("event-1", "filtered"));
    auto result = f.store.Page("event-1", 1, 10);
    REQUIRE(result["total"] == 1);
    REQUIRE(result["rows"][0]["result"]["decision"] == "unknown");
    REQUIRE(result["rows"][0]["publication"] == "published");
    REQUIRE(f.store.Page("x' OR 1=1 --", 1, 100)["total"] == 0);
    cosmo::LayaReviewStore reopened(f.path);
    REQUIRE(reopened.Page("event-1", 1, 10)["rows"][0]["result"]["reason"] == "deadline_exceeded");
}
TEST_CASE("Laya unfinished requests become unknown after restart", "[laya][review]") {
    StoreFixture f;
    REQUIRE(f.store.Begin(Identity()));
    cosmo::LayaReviewStore reopened(f.path);
    REQUIRE(reopened.Page("", 1, 10)["rows"][0]["result"]["reason"] == "engine_restarted");
}
TEST_CASE("Laya timeout includes image preparation and late results cannot overwrite", "[laya][review]") {
    StoreFixture f;
    std::atomic<int> calls{0};
    cosmo::LayaReview service(f.store, [&](const auto&, const auto& request, const auto&) {
        ++calls;
        return Response(request);
    });
    auto run   = std::make_shared<cosmo::LayaShadowRun>("epoch-1");
    auto start = std::chrono::steady_clock::now();
    REQUIRE(service.Submit(
        Identity(), run,
        [] {
            std::this_thread::sleep_for(150ms);
            return Image();
        },
        true, 30ms));
    REQUIRE(std::chrono::steady_clock::now() - start < 120ms);
    std::this_thread::sleep_for(180ms);
    REQUIRE(calls == 0);
    REQUIRE(f.store.Page("", 1, 10)["rows"][0]["result"]["reason"] == "deadline_exceeded");
}
TEST_CASE("Laya observe is asynchronous and stopped runs never infer", "[laya][review]") {
    StoreFixture f;
    std::atomic<int> calls{0};
    cosmo::LayaReview service(f.store, [&](const auto&, const auto& request, const auto&) {
        ++calls;
        return Response(request);
    });
    auto run = std::make_shared<cosmo::LayaShadowRun>("epoch-1");
    run->Invalidate();
    REQUIRE(service.Submit(Identity(), run, Image, false));
    for (int i = 0; i < 100 && f.store.Page("", 1, 10)["rows"][0]["result"].is_null(); ++i)
        std::this_thread::sleep_for(5ms);
    REQUIRE(calls == 0);
    REQUIRE(f.store.Page("", 1, 10)["rows"][0]["result"]["reason"] == "stale_task_run");
}
TEST_CASE("Laya queue saturation releases alarms without unbounded work", "[laya][review]") {
    StoreFixture f;
    std::promise<void> release;
    auto gate = release.get_future().share();
    cosmo::LayaReview service(
        f.store, [](const auto&, const auto& request, const auto&) { return Response(request); });
    auto run = std::make_shared<cosmo::LayaShadowRun>("epoch-1");
    for (int i = 0; i < 3; ++i)
        REQUIRE(service.Submit(
            Identity("event-" + std::to_string(i)), run,
            [gate] {
                gate.wait();
                return Image();
            },
            false));
    REQUIRE(service.Submit(Identity("saturated"), run, Image, true));
    auto result = f.store.Page("saturated", 1, 10);
    release.set_value();
    REQUIRE(result["rows"][0]["result"]["reason"] == "queue_full");
}
