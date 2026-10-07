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
    auto options              = Options();
    options.maxConfigurations = 3;
    VisualQuestionServiceImpl service(options,
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
    auto options              = Options();
    options.maxConfigurations = 3;
    VisualQuestionServiceImpl service(options,
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

TEST_CASE("Large visual catalogs compile bounded batches with one deadline and atomic activation",
          "[visual-preparation][catalog]") {
    const int fault = GENERATE(0, 1, 2);
    std::vector<VisualQuestionSpec> specs;
    for (int i = 0; i < 81; ++i) {
        auto spec           = Specs()[0];
        spec.itemId         = "label-" + std::to_string(i);
        spec.question["id"] = spec.itemId;
        specs.push_back(std::move(spec));
    }
    auto run  = Run();
    int calls = 0;
    std::vector<size_t> batches;
    std::vector<std::chrono::steady_clock::time_point> deadlines;
    VisualQuestionServiceImpl service(Options(),
                                      [&](const auto&, const auto& input, auto deadline, const auto&) {
                                          ++calls;
                                          batches.push_back(Json::parse(input)["questions"].size());
                                          deadlines.push_back(deadline);
                                          auto result         = Receipt(input);
                                          result["cache_hit"] = true;
                                          if (calls == 2 && fault == 1)
                                              result["binding"]["manifest_sha256"] = std::string(64, 'f');
                                          if (calls == 2 && fault == 2)
                                              run->Invalidate();
                                          return Output(result);
                                      });
    auto result = service.Prepare(specs, run, 5s).get();
    if (fault == 0) {
        REQUIRE(result.ready);
        REQUIRE(result.questions.size() == 81);
        CHECK(batches == std::vector<size_t>{32, 32, 17});
        CHECK(result.questions.back().itemId == "label-80");
        CHECK(result.cacheHit);
    } else {
        CHECK_FALSE(result.ready);
        CHECK(result.questions.empty());
        CHECK(calls == 2);
        CHECK(result.reason == (fault == 1 ? "invalid_compiler_receipt" : "stale_task_run"));
    }
    CHECK(std::all_of(deadlines.begin(), deadlines.end(), [&](auto d) { return d == deadlines.front(); }));
}

TEST_CASE("Visual catalog batching obeys byte and aggregate limits without truncation",
          "[visual-preparation][catalog]") {
    size_t calls = 0;
    VisualQuestionServiceImpl service(Options(), [&](const auto&, const auto& input, auto, const auto&) {
        ++calls;
        CHECK(input.size() <= visual::kMaxJson);
        CHECK(Json::parse(input)["questions"].size() <= 32);
        return Output(Receipt(input));
    });
    auto one                     = Specs()[0];
    one.question["instructions"] = std::string(4000, 'q');
    std::vector<VisualQuestionSpec> specs;
    for (int i = 0; i < 20; ++i) {
        auto spec           = one;
        spec.itemId         = "q-" + std::to_string(i);
        spec.question["id"] = spec.itemId;
        specs.push_back(std::move(spec));
    }
    auto result = service.Prepare(specs, Run(), 5s).get();
    REQUIRE(result.ready);
    CHECK(result.questions.size() == 20);
    CHECK(calls == 2);
    const auto before            = calls;
    one.question["instructions"] = std::string(visual::kMaxJson + 1, 'x');
    CHECK(service.Prepare({one}, Run(), 5s).get().reason == "compile_request_too_large");
    CHECK(service.Prepare(std::vector<VisualQuestionSpec>(513, one), Run(), 5s).get().reason ==
          "invalid_compile_request");
    CHECK(calls == before);
}

TEST_CASE("Visual startup admits independent configurations with one physical compiler",
          "[visual-preparation][startup]") {
    std::promise<void> entered;
    std::atomic<bool> release{false};
    std::atomic<int> calls{0}, active{0}, maximum{0};
    VisualQuestionServiceImpl service(Options(),
                                      [&](const auto&, const auto& input, auto, const auto& cancelled) {
                                          maximum = std::max(maximum.load(), ++active);
                                          if (++calls == 1) {
                                              entered.set_value();
                                              while (!release && !cancelled())
                                                  std::this_thread::sleep_for(1ms);
                                          }
                                          --active;
                                          return Output(Receipt(input));
                                      });
    std::vector<std::shared_future<VisualQuestionPreparation>> results;
    results.push_back(service.Prepare(Specs(), Run(), 5s));
    entered.get_future().wait();
    for (int i = 1; i < 19; ++i)
        results.push_back(service.Prepare(
            Specs(), std::make_shared<VisualDecisionRun>("task-" + std::to_string(i), "run", "revision"),
            5s));
    const auto pending = service.Counters();
    CHECK(pending["outstanding"] == 19);
    CHECK(pending["queued"] == 18);
    CHECK(pending["active"]["attempts"] == 1);
    CHECK(pending["pending"].size() == 18);
    CHECK(pending.dump().find("帽?") == std::string::npos);
    release = true;
    for (auto& result : results)
        CHECK(result.get().ready);
    CHECK(calls == 19);
    CHECK(maximum == 1);
    const auto finished = service.Counters();
    CHECK(finished["completed"] == 19);
    CHECK(finished["outstanding"] == 0);
    CHECK(finished["input_bytes"] == 0);
    CHECK(finished["active"].is_null());
}

TEST_CASE("Visual startup rotates catalogs without exposing partial preparation",
          "[visual-preparation][startup]") {
    std::vector<VisualQuestionSpec> catalog;
    for (int i = 0; i < 81; ++i) {
        auto spec           = Specs()[0];
        spec.itemId         = "large-" + std::to_string(i);
        spec.question["id"] = spec.itemId;
        catalog.push_back(spec);
    }
    std::promise<void> firstBatch, secondBatch;
    std::atomic<bool> releaseFirst{false}, releaseSecond{false};
    std::vector<std::string> order;
    std::vector<std::chrono::steady_clock::time_point> catalogDeadlines;
    VisualQuestionServiceImpl service(
        Options(), [&](const auto&, const auto& input, auto deadline, const auto& cancelled) {
            const std::string id = Json::parse(input)["questions"][0]["question"]["id"];
            order.push_back(id);
            if (id.find("large-") == 0)
                catalogDeadlines.push_back(deadline);
            if (id == "large-0") {
                firstBatch.set_value();
                while (!releaseFirst && !cancelled())
                    std::this_thread::sleep_for(1ms);
            }
            if (id == "large-32") {
                secondBatch.set_value();
                while (!releaseSecond && !cancelled())
                    std::this_thread::sleep_for(1ms);
            }
            return Output(Receipt(input));
        });
    auto large = service.Prepare(catalog, Run(), 5s);
    firstBatch.get_future().wait();
    auto small   = service.Prepare(Specs(), Run("small"), 5s);
    releaseFirst = true;
    auto second  = secondBatch.get_future();
    REQUIRE(second.wait_for(2s) == std::future_status::ready);
    CHECK(small.wait_for(0ms) == std::future_status::ready);
    CHECK(large.wait_for(0ms) == std::future_status::timeout);
    CHECK(service.Counters()["active"]["prepared_questions"] == 32);
    releaseSecond = true;
    CHECK(small.get().ready);
    const auto result = large.get();
    REQUIRE(result.ready);
    CHECK(result.questions.size() == 81);
    CHECK(order == std::vector<std::string>{"large-0", "helmet", "large-32", "large-64"});
    CHECK(std::all_of(catalogDeadlines.begin(), catalogDeadlines.end(),
                      [&](auto deadline) { return deadline == catalogDeadlines.front(); }));
}

TEST_CASE("Visual startup retries only bounded transient process failures with one deadline",
          "[visual-preparation][startup]") {
    const std::string failure =
        GENERATE("process_spawn_failed", "process_io_failed", "process_output_limit", "process_timeout",
                 "sequence_budget_exceeded", "receipt_spawn_failure");
    const bool transient = failure == "process_spawn_failed" || failure == "process_io_failed";
    const bool exhausted = GENERATE(false, true);
    auto options         = Options();
    options.retryDelay   = 1ms;
    std::vector<std::string> inputs;
    std::vector<std::chrono::steady_clock::time_point> deadlines;
    VisualQuestionServiceImpl service(options, [&](const auto&, const auto& input, auto deadline,
                                                   const auto&) {
        inputs.push_back(input);
        deadlines.push_back(deadline);
        if (inputs.size() > 2 && !exhausted)
            return Output(Receipt(input));
        if (failure == "sequence_budget_exceeded" || failure == "receipt_spawn_failure") {
            auto result =
                Output({{"status", "rejected"},
                        {"reason", failure == "receipt_spawn_failure" ? "process_spawn_failed" : failure}});
            result.exitCode = 1;
            return result;
        }
        cosmo::util::BoundedProcessResult result;
        result.failure = failure;
        return result;
    });
    const auto result = service.Prepare(Specs(), Run(), 5s).get();
    CHECK(result.ready == (transient && !exhausted));
    CHECK(inputs.size() == (transient ? 3 : 1));
    CHECK(service.Counters()["retried"] == (transient ? 2 : 0));
    if (!result.ready) {
        CHECK(result.questions.empty());
        CHECK(result.reason == (failure == "receipt_spawn_failure" ? "process_spawn_failed" : failure));
    }
    CHECK(std::all_of(inputs.begin(), inputs.end(),
                      [&](const auto& input) { return input == inputs.front(); }));
    CHECK(std::all_of(deadlines.begin(), deadlines.end(),
                      [&](auto deadline) { return deadline == deadlines.front(); }));
}

TEST_CASE("Queued visual cancellation and deadline settle while a different compiler is busy",
          "[visual-preparation][startup]") {
    std::promise<void> entered;
    std::atomic<bool> release{false};
    std::atomic<int> calls{0};
    VisualQuestionServiceImpl service(Options(),
                                      [&](const auto&, const auto& input, auto, const auto& cancelled) {
                                          if (++calls == 1) {
                                              entered.set_value();
                                              while (!release && !cancelled())
                                                  std::this_thread::sleep_for(1ms);
                                          }
                                          return Output(Receipt(input));
                                      });
    auto active = service.Prepare(Specs(), Run(), 5s);
    entered.get_future().wait();
    auto obsolete  = Run("obsolete");
    auto cancelled = service.Prepare(Specs(), obsolete, 5s);
    auto expired   = service.Prepare(Specs(), Run("expired"), 100ms);
    obsolete->Invalidate();
    CHECK(cancelled.wait_for(2s) == std::future_status::ready);
    CHECK(expired.wait_for(2s) == std::future_status::ready);
    CHECK(active.wait_for(0ms) == std::future_status::timeout);
    CHECK(calls == 1);
    CHECK(service.Counters()["outstanding"] == 1);
    release = true;
    CHECK(cancelled.get().reason == "stale_task_run");
    CHECK(expired.get().reason == "compile_deadline_exceeded");
    CHECK(active.get().ready);
    CHECK(service.Counters()["failed"] == 2);
    CHECK(service.Counters()["input_bytes"] == 0);
}

TEST_CASE("Visual startup bounds retained input bytes and reclaims cancelled configurations",
          "[visual-preparation][startup]") {
    Json input = {{"questions", Json::array()}};
    for (const auto& spec : Specs())
        input["questions"].push_back({{"question", spec.question}, {"text_state", spec.textState}});
    auto options          = Options();
    options.maxInputBytes = input.dump().size() * 2;
    std::promise<void> entered;
    std::atomic<bool> release{false};
    std::atomic<int> calls{0};
    VisualQuestionServiceImpl service(options,
                                      [&](const auto&, const auto& encoded, auto, const auto& cancelled) {
                                          if (++calls == 1) {
                                              entered.set_value();
                                              while (!release && !cancelled())
                                                  std::this_thread::sleep_for(1ms);
                                          }
                                          return Output(Receipt(encoded));
                                      });
    auto active = service.Prepare(Specs(), Run(), 5s);
    entered.get_future().wait();
    auto obsolete = Run("obsolete");
    auto old      = service.Prepare(Specs(), obsolete, 5s);
    CHECK(service.Prepare(Specs(), Run("too-large"), 5s).get().reason == "compiler_queue_bytes_full");
    CHECK(service.Counters()["input_bytes"] == options.maxInputBytes);
    obsolete->Invalidate();
    auto replacement = service.Prepare(Specs(), Run("replacement"), 5s);
    CHECK(service.Counters()["input_bytes"] <= options.maxInputBytes);
    release = true;
    CHECK(old.get().reason == "stale_task_run");
    CHECK(active.get().ready);
    CHECK(replacement.get().ready);
    CHECK(service.Counters()["input_bytes"] == 0);
    CHECK(service.Counters()["rejected"] == 1);
}

TEST_CASE("Visual retry backoff yields admission without extending a deadline",
          "[visual-preparation][startup]") {
    auto options       = Options();
    options.retryDelay = 5s;
    std::promise<void> entered;
    std::atomic<int> calls{0};
    VisualQuestionServiceImpl service(options, [&](const auto&, const auto& input, auto, const auto&) {
        if (++calls == 1) {
            entered.set_value();
            cosmo::util::BoundedProcessResult result;
            result.failure = "process_io_failed";
            return result;
        }
        return Output(Receipt(input));
    });
    auto retry = service.Prepare(Specs(), Run(), 100ms);
    entered.get_future().wait();
    auto other = service.Prepare(Specs(), Run("other"), 5s);
    REQUIRE(other.wait_for(2s) == std::future_status::ready);
    CHECK(other.get().ready);
    REQUIRE(retry.wait_for(2s) == std::future_status::ready);
    CHECK(retry.get().reason == "compile_deadline_exceeded");
    CHECK(calls == 2);
    CHECK(service.Counters()["retried"] == 1);
    CHECK(service.Counters()["outstanding"] == 0);
}

TEST_CASE("Visual catalog retry resumes the failed batch and preserves complete source identity",
          "[visual-preparation][startup]") {
    auto options       = Options();
    options.retryDelay = 1ms;
    std::vector<VisualQuestionSpec> catalog;
    Json input = {{"questions", Json::array()}};
    for (int i = 0; i < 65; ++i) {
        auto spec           = Specs()[0];
        spec.itemId         = "catalog-" + std::to_string(i);
        spec.question["id"] = spec.itemId;
        catalog.push_back(spec);
        input["questions"].push_back({{"question", spec.question}, {"text_state", spec.textState}});
    }
    std::vector<std::string> order;
    VisualQuestionServiceImpl service(options, [&](const auto&, const auto& encoded, auto, const auto&) {
        const std::string id = Json::parse(encoded)["questions"][0]["question"]["id"];
        order.push_back(id);
        if (order.size() == 2) {
            cosmo::util::BoundedProcessResult result;
            result.failure = "process_io_failed";
            return result;
        }
        auto result         = Receipt(encoded);
        result["cache_hit"] = id != "catalog-64";
        return Output(result);
    });
    const auto result = service.Prepare(catalog, Run(), 5s).get();
    REQUIRE(result.ready);
    REQUIRE(result.questions.size() == 65);
    for (size_t i = 0; i < catalog.size(); ++i)
        CHECK(result.questions[i].itemId == catalog[i].itemId);
    const auto encoded = input.dump();
    CHECK(result.inputSha256 ==
          visual::Sha256(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()));
    CHECK(result.manifestSha256 == options.manifestSha256);
    CHECK_FALSE(result.cacheHit);
    CHECK(order == std::vector<std::string>{"catalog-0", "catalog-32", "catalog-32", "catalog-64"});
    CHECK(service.Counters()["retried"] == 1);
}
