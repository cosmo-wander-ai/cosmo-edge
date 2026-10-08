#include <atomic>

#include "catch_amalgamated.hpp"
#include "service/ai/impl/NativeVisualBackend.h"

using namespace cosmo::service;
using Json = nlohmann::json;
using namespace std::chrono_literals;

namespace {
class BoundLlm final : public ILlmInferService {
public:
    std::atomic<int> users{0}, calls{0};
    std::string bound;
    bool EnsureInit(const std::string&) override {
        return false;
    }
    bool IsInitialized() const override {
        return true;
    }
    cosmo::util::ErrorEnum Generate(const std::vector<VideoFramePtr>&, const std::vector<std::string>&,
                                    const cosmo::Qwen3VLGenerationParam&,
                                    std::vector<cosmo::Qwen3VLResult>&) override {
        FAIL("Native visual inference must retain its explicit model binding");
        return cosmo::util::ErrorEnum::Failed;
    }
    bool PrepareText(const std::string& model, const std::string& input, std::string& output,
                     std::string& identity) override {
        ++calls;
        bound              = model;
        identity           = std::string(64, 'a');
        Json rows          = Json::array();
        const auto request = Json::parse(input);
        for (const auto& spec : request.at("questions")) {
            REQUIRE(spec.at("text_state") == "state");
            rows.push_back({{"qtype", 2},
                            {"ordered_options", {"false", "true"}},
                            {"temperature",
                             {{"value", 1.0}, {"bucket", "image|noul:2"}, {"source", "temperature_image"}}},
                            {"tokens", {2, 100, 1}},
                            {"markers", {1}},
                            {"image_position", 3}});
        }
        output = Json{{"backend", "laya_v"}, {"questions", rows}}.dump();
        return true;
    }
    cosmo::util::ErrorEnum GetMaxBatchSize(size_t&) const override {
        return cosmo::util::ErrorEnum::Success;
    }
    void Reset() override {}
    void NotifyWorkerStart() override {
        ++users;
    }
    void NotifyWorkerStop() override {
        --users;
    }
};
std::vector<VisualQuestionSpec> Catalog() {
    std::vector<VisualQuestionSpec> result;
    for (int i = 0; i < 40; ++i) {
        auto id = "question-" + std::to_string(i);
        result.push_back(
            {id, {{"id", id}, {"version", 1}, {"type", "noul"}, {"instructions", "helmet"}}, "state"});
    }
    return result;
}
}  // namespace

TEST_CASE("Native catalog uses task model binding across batches without a compiler process",
          "[native-visual]") {
    BoundLlm llm;
    auto run   = std::make_shared<VisualDecisionRun>("task", "epoch", "revision", "model-from-repository");
    auto specs = Catalog();
    {
        VisualQuestionServiceImpl service(
            {},
            [](const auto&, const auto&, auto, const auto&) -> cosmo::util::BoundedProcessResult {
                FAIL("Native preparation must not spawn Python");
                return {};
            },
            NativeVisualCompiler(llm));
        auto pending = service.Prepare(specs, run, 5s);
        REQUIRE(pending.wait_for(5s) == std::future_status::ready);
        auto result = pending.get();
        REQUIRE(result.ready);
        REQUIRE(result.questions.size() == 40);
        REQUIRE(result.manifestSha256 == std::string(64, 'a'));
        REQUIRE(llm.bound == run->atomicCode);
        REQUIRE(llm.calls == 2);
        REQUIRE(llm.users == 1);
        REQUIRE(result.questions.front().nativeSpec.at("text_state") == "state");
        auto changed                        = specs;
        changed[0].question["instructions"] = "fire";
        auto changedResult                  = NativeVisualCompiler(llm)(changed, run);
        REQUIRE(changedResult.questions.front().compiledSha256 != result.questions.front().compiledSha256);
        run->Invalidate();
        REQUIRE_FALSE(NativeVisualCompiler(llm)(specs, run).ready);
        service.Stop();
    }
    run.reset();
    REQUIRE(llm.users == 0);
}

TEST_CASE("Native decisions retain model identity and never invoke the socket transport", "[native-visual]") {
    BoundLlm llm;
    auto run      = std::make_shared<VisualDecisionRun>("task", "epoch", "revision", "imported-model");
    auto compiled = NativeVisualCompiler(llm)({Catalog().front()}, run);
    REQUIRE(compiled.ready);
    std::atomic<int> nativeCalls{0};
    VisualDecisionServiceImpl service(
        {},
        [](const auto&, const auto&, const auto&, auto) -> Json {
            FAIL("Native decisions must not invoke IPC");
            return {};
        },
        nullptr,
        [&](const auto& identity, const auto& questions, const auto& selected, const auto&, auto) -> Json {
            ++nativeCalls;
            REQUIRE(selected->atomicCode == "imported-model");
            REQUIRE(identity.at("manifest_sha256") == compiled.manifestSha256);
            REQUIRE(questions.front().nativeSpec.at("question").at("id") == "question-0");
            return visual::Failure(identity, "fixture");
        });
    VisualDecisionRequest request{"frame", "roi", compiled.questions};
    auto result = service.Decide(request, run, [] { return VisualDecisionImage{{1, 2, 3}, 16, 16}; }, 2s);
    REQUIRE(result.response.at("reason") == "fixture");
    REQUIRE(result.Retain());
    REQUIRE(nativeCalls == 1);
    REQUIRE(service.Counters().at("execution") == "in_engine");
    request.questions.front().modelIdentity.clear();
    REQUIRE(service.Decide(
                       request, run, [] { return VisualDecisionImage{}; }, 2s)
                .response.at("reason") == "bound_model_changed");
    REQUIRE(nativeCalls == 1);
}
