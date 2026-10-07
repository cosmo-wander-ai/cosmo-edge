#include <atomic>
#include <thread>

#include "catch_amalgamated.hpp"
#include "service/ai/impl/VisualDecisionProtocol.h"
#include "service/ai/impl/VisualQuestionServiceImpl.h"

using namespace std::chrono_literals;
namespace {
using namespace cosmo::service;
using Json = nlohmann::json;
auto Run(const std::string& epoch = "run-1") {
    return std::make_shared<VisualDecisionRun>("task", epoch, "revision");
}
VisualQuestionCompilerOptions Options() {
    VisualQuestionCompilerOptions result;
    result.script         = "/private/runtime/compile_questions.py";
    result.manifestPath   = "/private/manifest.json";
    result.manifestSha256 = std::string(64, 'a');
    result.binding        = {
        {"manifest_sha256", result.manifestSha256},
        {"assets",
                {{"tokenizer_sha256", std::string(64, 'b')}, {"model_config_sha256", std::string(64, 'c')}}},
        {"implementation", Json::object()}};
    for (const auto* name : {"compile_questions.py", "question_compiler.py", "image_frontend.py",
                             "visual_protocol.py", "laya_shadow_worker.py"})
        result.binding["implementation"][name] = std::string(64, 'd');
    return result;
}
std::vector<VisualQuestionSpec> Specs() {
    return {{"helmet", {{"id", "helmet"}, {"version", 2}, {"type", "noul"}, {"instructions", "帽?"}}, ""},
            {"choice",
             {{"id", "classification"},
              {"version", 3},
              {"type", "choice"},
              {"instructions", "哪类?"},
              {"criteria", Json::array({{{"label", "yes"}, {"description", "有"}},
                                        {{"label", "no"}, {"description", "无"}}})}},
             ""}};
}
Json Receipt(const std::string& input) {
    auto request = Json::parse(input);
    Json result  = {
        {"status", "prepared"},
        {"business_qualified", false},
        {"cache_hit", false},
        {"input_sha256", visual::Sha256(reinterpret_cast<const uint8_t*>(input.data()), input.size())},
        {"binding", Options().binding},
        {"questions", Json::array()}};
    for (const auto& row : request["questions"]) {
        const auto& q     = row["question"];
        const bool choice = q["type"] == "choice";
        std::vector<std::string> options{"false", "true"};
        if (choice) {
            options.clear();
            for (const auto& criterion : q["criteria"])
                options.push_back(criterion["label"]);
        }
        result["questions"].push_back(
            {{"question_id", q["id"]},
             {"question_version", q["version"]},
             {"compiled_sha256", std::string(64, 'e')},
             {"qtype", choice ? 0 : 2},
             {"ordered_options", options},
             {"temperature", {{"value", 1.0}, {"bucket", "test"}, {"source", "test"}}},
             {"bindings", Options().binding["assets"]}});
    }
    return result;
}
cosmo::util::BoundedProcessResult Output(const Json& value) {
    cosmo::util::BoundedProcessResult result;
    result.exitCode = 0;
    result.output   = value.dump();
    return result;
}
}  // namespace

TEST_CASE("Visual configuration keeps order and binds receipts to the exact source and release",
          "[visual-preparation]") {
    const int fault = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    std::vector<std::string> arguments;
    VisualQuestionServiceImpl service(Options(), [&](const auto& argv, const auto& input, auto, const auto&) {
        arguments   = argv;
        auto result = Receipt(input);
        switch (fault) {
            case 1:
                result["input_sha256"] = std::string(64, 'f');
                break;
            case 2:
                result["binding"]["manifest_sha256"] = std::string(64, 'f');
                break;
            case 3:
                result["questions"][0]["question_version"] = 4;
                break;
            case 4:
                result["questions"][1]["ordered_options"] = {"no", "yes"};
                break;
            case 5:
                result["questions"][0]["compiled_sha256"] = "../invalid";
                break;
            case 6:
                result["business_qualified"] = true;
                break;
            case 7:
                result["questions"][0]["qtype"] = 2.1;
                break;
        }
        return Output(result);
    });
    auto result = service.Prepare(Specs(), Run(), 5s).get();
    REQUIRE(result.ready == (fault == 0));
    REQUIRE(std::find(arguments.begin(), arguments.end(), "帽?") == arguments.end());
    if (fault == 0) {
        REQUIRE(result.questions.size() == 2);
        REQUIRE(result.questions[1].orderedOptions == std::vector<std::string>{"yes", "no"});
        REQUIRE(result.questions[1].itemId == "choice");
        REQUIRE(result.manifestSha256 == Options().manifestSha256);
    } else
        REQUIRE(result.questions.empty());
}

TEST_CASE("Visual configuration rejects ambiguous option maps before spawning compiler",
          "[visual-preparation]") {
    int calls = 0;
    VisualQuestionServiceImpl service(Options(), [&](const auto&, const auto& input, auto, const auto&) {
        ++calls;
        return Output(Receipt(input));
    });
    auto specs                    = Specs();
    specs[1].question["criteria"] = {{"yes", "有"}, {"no", "无"}};
    REQUIRE(service.Prepare(specs, Run(), 5s).get().reason == "choice_requires_ordered_criteria");
    specs           = Specs();
    specs[1].itemId = specs[0].itemId;
    REQUIRE(service.Prepare(specs, Run(), 5s).get().reason == "invalid_compile_request");
    REQUIRE(calls == 0);
}

TEST_CASE("Visual compiler serializes cold allocations and cancels obsolete configurations",
          "[visual-preparation]") {
    std::promise<void> entered;
    std::atomic<int> active{0}, maximum{0}, calls{0};
    VisualQuestionServiceImpl service(Options(),
                                      [&](const auto&, const auto& input, auto, const auto& cancelled) {
                                          const int concurrent = ++active;
                                          maximum              = std::max(maximum.load(), concurrent);
                                          if (++calls == 1) {
                                              entered.set_value();
                                              while (!cancelled())
                                                  std::this_thread::sleep_for(1ms);
                                          }
                                          --active;
                                          return Output(Receipt(input));
                                      });
    auto old   = Run();
    auto first = service.Prepare(Specs(), old, 5s);
    entered.get_future().wait();
    auto second   = service.Prepare(Specs(), Run("run-2"), 5s);
    auto third    = service.Prepare(Specs(), Run("run-3"), 5s);
    auto rejected = service.Prepare(Specs(), Run("run-4"), 5s).get();
    old->Invalidate();
    const auto firstResult  = first.get();
    const auto secondResult = second.get();
    const auto thirdResult  = third.get();
    REQUIRE(rejected.reason == "compiler_queue_full");
    REQUIRE(firstResult.reason == "stale_task_run");
    REQUIRE(secondResult.ready);
    REQUIRE(thirdResult.ready);
    REQUIRE(maximum == 1);
    REQUIRE(calls == 3);
}

TEST_CASE("Visual preparation retains compiler errors and stop cancels the owned process",
          "[visual-preparation]") {
    SECTION("errors do not expose partially compiled refs") {
        VisualQuestionServiceImpl service(Options(), [](const auto&, const auto&, auto, const auto&) {
            auto result     = Output({{"status", "rejected"}, {"reason", "sequence_budget_exceeded"}});
            result.exitCode = 1;
            return result;
        });
        auto result = service.Prepare(Specs(), Run(), 5s).get();
        REQUIRE_FALSE(result.ready);
        REQUIRE(result.reason == "sequence_budget_exceeded");
        REQUIRE(result.questions.empty());
    }
    SECTION("stop signals cancellation and settles queued handles") {
        std::promise<void> entered;
        VisualQuestionServiceImpl service(Options(),
                                          [&](const auto&, const auto& input, auto, const auto& cancelled) {
                                              entered.set_value();
                                              while (!cancelled())
                                                  std::this_thread::sleep_for(1ms);
                                              return Output(Receipt(input));
                                          });
        auto first = service.Prepare(Specs(), Run(), 5s);
        entered.get_future().wait();
        auto queued = service.Prepare(Specs(), Run("run-2"), 5s);
        service.Stop();
        REQUIRE(first.get().reason == "service_stopped");
        REQUIRE(queued.get().reason == "service_stopped");
    }
}

TEST_CASE("Replacing pending visual configurations frees stale queue slots", "[visual-preparation]") {
    std::promise<void> entered;
    std::atomic<bool> release{false};
    VisualQuestionServiceImpl service(Options(),
                                      [&](const auto&, const auto& input, auto, const auto& cancelled) {
                                          if (!release.load()) {
                                              entered.set_value();
                                              while (!release && !cancelled())
                                                  std::this_thread::sleep_for(1ms);
                                          }
                                          return Output(Receipt(input));
                                      });
    auto first = service.Prepare(Specs(), Run("running"), 5s);
    entered.get_future().wait();
    auto old       = Run("old");
    auto oldFuture = service.Prepare(Specs(), old, 5s);
    auto other     = service.Prepare(Specs(), Run("other"), 5s);
    old->Invalidate();
    auto replacement = service.Prepare(Specs(), Run("replacement"), 5s);
    release          = true;
    CHECK(oldFuture.get().reason == "stale_task_run");
    CHECK(first.get().ready);
    CHECK(other.get().ready);
    CHECK(replacement.get().ready);
}
