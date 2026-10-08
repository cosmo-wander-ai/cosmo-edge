#include <future>
#include <nlohmann/json.hpp>
#include <string_view>

#include "catch_amalgamated.hpp"
#include "flow/logical/PictureRule.h"
#include "flow/task/PTaskBase.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/PictureWorkflow.h"

using namespace cosmo;

namespace {
ActionNode Node(const std::string& id, std::string_view kind, const std::string& parent = "-1") {
    ActionNode node;
    node.flowActionId    = id;
    node.preFlowActionId = parent;
    node.actionId        = kind;
    node.atomicCode      = "test-model";
    return node;
}

MsgDynamicKeyValue Param(const std::string& key, const std::string& value) {
    MsgDynamicKeyValue param;
    param.key   = key;
    param.value = value;
    return param;
}

LogicCalc Compare(const std::string& left, const std::string& right, LogicType op = LogicType::GE) {
    LogicCalc rule;
    rule.keyL = left;
    rule.keyR = right;
    rule.type = op;
    return rule;
}

class FakePictureAction : public PActionBase {
public:
    explicit FakePictureAction(ActionNode& node) : PActionBase(node, "picture-test") {}
    util::ErrorEnum status{util::ErrorEnum::Success};
    size_t seen{0};
    bool modelIds{false};
    std::vector<MsgDynamicKeyValue> parameters;
    bool SetParam(const std::string&, std::vector<MsgDynamicKeyValue>& values) override {
        parameters = values;
        return true;
    }
    util::ErrorEnum HandPic(AlgDataPtr data) override {
        seen = data->chanDataDetect.detRet ? data->chanDataDetect.detRet->targets.size() : 0;
        if (modelIds && data->chanDataDetect.detRet)
            for (auto& target : data->chanDataDetect.detRet->targets)
                target.targetId = "model-generated-uuid";
        return status;
    }
};

AlgDataPtr Input() {
    auto data                   = std::make_shared<AlgData>();
    data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    AiDetectRstEl target;
    target.confidence.label      = "person";
    target.confidence.confidence = 0.8F;
    target.box                   = util::Box(5, 10, 40, 50);
    data->chanDataDetect.detRet->targets.push_back(target);
    return data;
}

void AddRule(PTaskElementPtr task, ActionNode node) {
    PTaskAction entry;
    entry.action     = node;
    entry.actionInst = std::make_shared<PictureRuleAction>("picture-test", node);
    task->actions.push_back(std::move(entry));
}

auto Task() {
    auto task                       = std::make_shared<PTaskElement>();
    task->taskId                    = "picture-test";
    task->action_alg                = std::make_shared<ActionAlg>();
    task->action_alg->algorithmName = "image scenario";
    return task;
}
}  // namespace

TEST_CASE("Picture workflows validate topology and capabilities", "[picture][workflow]") {
    std::string error;
    auto output                 = Node("out", PAOutput_Code, "rule");
    auto detect                 = Node("det", PADetect_Code);
    auto rule                   = Node("rule", PALogicalJudgment_Code, "det");
    rule.configObject.condition = Compare("aiOut.person.threshold", "0.5");
    std::vector<ActionNode> graph{output, rule, detect};
    REQUIRE(ValidatePictureWorkflow(graph, error));
    REQUIRE(graph.front().flowActionId == "det");
    REQUIRE(graph.back().flowActionId == "out");
    SECTION("duplicate") {
        graph.push_back(detect);
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
    SECTION("cycle") {
        graph.front().preFlowActionId = "out";
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
    SECTION("dangling") {
        graph.front().preFlowActionId = "missing";
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
    SECTION("region") {
        graph.front().actionId = "BA_00005";
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
    SECTION("feature prerequisites") {
        graph.front().actionId = PAMatch_Code;
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
    SECTION("model missing") {
        graph.front().atomicCode.clear();
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
    SECTION("invalid nested rule") {
        graph[1].configObject.condition.type = LogicType::AND;
        graph[1].configObject.condition.list = {LogicCalc{}};
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
    SECTION("rule references must come from an ancestor result") {
        graph[1].configObject.condition = Compare("node.out", "1");
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
        graph[1].configObject.condition = Compare("node.det", "1");
        REQUIRE_FALSE(ValidatePictureWorkflow(graph, error));
    }
}

TEST_CASE("Picture rules retain unknown evidence through NOT and aggregates", "[picture][workflow]") {
    auto data       = Input();
    auto& target    = data->chanDataDetect.detRet->targets.front();
    target.targetId = "det:0";
    auto missing    = Compare("aiOut.absent.threshold", "0.5");
    REQUIRE_FALSE(EvaluatePictureRule(missing, *data, &target, {}).has_value());
    LogicCalc negate;
    negate.type = LogicType::NOR;
    negate.list = {missing};
    REQUIRE_FALSE(EvaluatePictureRule(negate, *data, &target, {}).has_value());
    data->bHaveLogic                        = true;
    data->pictureDecisions[target.targetId] = "unknown";
    REQUIRE_FALSE(EvaluatePictureRule(Compare("picture.matchedCount", "0"), *data, nullptr, {}).has_value());
    REQUIRE(EvaluatePictureRule(Compare("picture.count", "1"), *data, nullptr, {}) == true);
    target.bFilter = true;
    REQUIRE(EvaluatePictureRule(Compare("picture.count", "0", LogicType::Equal), *data, nullptr, {}) == true);
}

TEST_CASE("Picture business results execute rules and reset request parameters", "[picture][workflow]") {
    PTaskBase runtime;
    auto task      = Task();
    auto detect    = Node("det", PADetect_Code);
    auto fake      = std::make_shared<FakePictureAction>(detect);
    fake->modelIds = true;
    PTaskAction entry;
    entry.action     = detect;
    entry.actionInst = fake;
    task->actions.push_back(entry);
    auto rule                   = Node("rule", PALogicalJudgment_Code, "det");
    rule.configObject.condition = Compare("aiOut.person.threshold", "custom.threshold");
    rule.configObject.params    = {Param("custom.threshold", "0.5")};
    AddRule(task, rule);
    AddRule(task, Node("out", PAOutput_Code, "rule"));
    MsgPTaskDetectPicRecv request;
    request.requestId         = "request-one";
    request.taskConfig.params = {Param("custom.threshold", "0.9")};
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    REQUIRE(runtime.ExecutePicture(task, Input(), request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(response.resData.outputs.front().decision == "not_matched");
    REQUIRE(response.resData.targetList.empty());
    request.taskConfig.params.clear();
    REQUIRE(runtime.ExecutePicture(task, Input(), request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(response.resData.outputs.front().decision == "matched");
    REQUIRE(response.resData.targetList.size() == 1);
    REQUIRE(response.resData.targetList.front().rules.at("rule") == "matched");
    REQUIRE(response.resData.targetList.front().sourceNodeId == "det");
    nlohmann::json json = response;
    REQUIRE(json["resData"]["schemaVersion"] == 2);
    REQUIRE_FALSE(json["resData"].contains("areaList"));
    REQUIRE(json.get<MsgPTaskDetectPicSend>().resData.targetList.size() == 1);
    request.resultMode = "legacy";
    REQUIRE(runtime.ExecutePicture(task, Input(), request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(nlohmann::json(response)["resData"].contains("areaList"));
}

TEST_CASE("Picture forks isolate filtering and stop on inference failures", "[picture][workflow]") {
    PTaskBase runtime;
    auto task   = Task();
    auto detect = Node("det", PADetect_Code);
    PTaskAction entry;
    entry.action     = detect;
    entry.actionInst = std::make_shared<FakePictureAction>(detect);
    task->actions.push_back(entry);
    auto filter                = Node("filter", PAFilter_Code, "det");
    filter.configObject.params = {Param("filter.person.side.min", "60")};
    AddRule(task, filter);
    auto classify    = Node("classify", PAClassify_Code, "filter");
    auto fake        = std::make_shared<FakePictureAction>(classify);
    entry.action     = classify;
    entry.actionInst = fake;
    task->actions.push_back(entry);
    AddRule(task, Node("filtered-output", PAOutput_Code, "classify"));
    AddRule(task, Node("original-output", PAOutput_Code, "det"));
    MsgPTaskDetectPicRecv request;
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    auto input = Input();
    REQUIRE(runtime.ExecutePicture(task, input, request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(fake->seen == 0);
    REQUIRE(response.resData.outputs[0].matchedCount == 0);
    REQUIRE(response.resData.outputs[1].matchedCount == 1);
    REQUIRE_FALSE(input->chanDataDetect.detRet->targets.front().bFilter);
    fake->status = util::ErrorEnum::NotInit;
    REQUIRE(runtime.ExecutePicture(task, input, request, response, rendered) == util::ErrorEnum::NotInit);
    REQUIRE(response.resData.status == "failed");
    REQUIRE(response.resData.errorNodeId == "classify");
    REQUIRE(response.resData.outputs.empty());
    REQUIRE(response.resData.targetList.empty());
}

TEST_CASE("Picture conditional branches skip downstream work and preserve unknown", "[picture][workflow]") {
    PTaskBase runtime;
    auto task                     = Task();
    auto branch                   = Node("branch", PABranch_Code);
    branch.configObject.condition = Compare("custom.missing", "1");
    AddRule(task, branch);
    auto detect  = Node("det", PADetect_Code, "branch");
    auto fake    = std::make_shared<FakePictureAction>(detect);
    fake->status = util::ErrorEnum::Failed;
    PTaskAction entry;
    entry.action     = detect;
    entry.actionInst = fake;
    task->actions.push_back(entry);
    MsgPTaskDetectPicRecv request;
    request.resultMode = "debug";
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    REQUIRE(runtime.ExecutePicture(task, Input(), request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(response.resData.outputs.front().decision == "unknown");
    REQUIRE(response.resData.nodes.back().status == "skipped");
    request.taskConfig.areas.emplace_back();
    REQUIRE(runtime.ExecutePicture(task, Input(), request, response, rendered) ==
            util::ErrorEnum::InvalidParam);
}

TEST_CASE("Picture task leases release locks and reject retired tasks", "[picture][workflow]") {
    auto task = Task();
    {
        PictureTaskLease lease(task);
        REQUIRE(static_cast<bool>(lease));
        REQUIRE(task->mtx.try_lock());
        task->mtx.unlock();
        auto waiting = std::async(std::launch::async, [task] {
            PictureTaskLease next(task);
            return static_cast<bool>(next);
        });
        {
            std::lock_guard<std::mutex> lock(task->mtx);
            task->retired = true;
        }
        task->idle.notify_all();
        REQUIRE_FALSE(waiting.get());
    }
    REQUIRE_FALSE(task->executing);
}

TEST_CASE("Picture parameters reject malformed thresholds", "[picture][workflow]") {
    REQUIRE(ValidatePictureParams({Param("aiParam.person.confidence", "0")}));
    REQUIRE_FALSE(ValidatePictureParams({Param("aiParam.person.confidence", "NaN")}));
    REQUIRE_FALSE(ValidatePictureParams({Param("aiParam.person.confidence", "1.1")}));
    REQUIRE_FALSE(ValidatePictureParams(
        {Param("filter.person.side.min", "60"), Param("filter.person.side.max", "40")}));
    REQUIRE_FALSE(ValidatePictureParams({Param("match.libraryType", "unknown")}));
}

TEST_CASE("Picture scenario defaults survive serialization and respect request precedence",
          "[picture][workflow]") {
    PTaskBase runtime;
    auto task                         = Task();
    task->action_alg->pictureDefaults = {Param("custom.threshold", "0.9")};
    nlohmann::json config             = *task->action_alg;
    task->action_alg                  = std::make_shared<ActionAlg>(config.get<ActionAlg>());
    REQUIRE(task->action_alg->pictureDefaults.size() == 1);
    auto detect = Node("det", PADetect_Code);
    PTaskAction entry;
    entry.action     = detect;
    entry.actionInst = std::make_shared<FakePictureAction>(detect);
    task->actions.push_back(entry);
    auto rule                   = Node("rule", PALogicalJudgment_Code, "det");
    rule.configObject.condition = Compare("aiOut.person.threshold", "custom.threshold");
    rule.configObject.params    = {Param("custom.threshold", "0.1")};
    AddRule(task, rule);
    MsgPTaskDetectPicRecv request;
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    REQUIRE(runtime.ExecutePicture(task, Input(), request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(response.resData.outputs.front().decision == "not_matched");
    request.taskConfig.params = {Param("custom.threshold", "0.5")};
    REQUIRE(runtime.ExecutePicture(task, Input(), request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(response.resData.outputs.front().decision == "matched");
}

TEST_CASE("Picture results retain library evidence independently of business hits", "[picture][workflow]") {
    PTaskBase runtime;
    auto task = Task();
    AddRule(task, Node("out", PAOutput_Code));
    auto input          = Input();
    input->bHaveLogic   = true;
    auto& target        = input->chanDataDetect.detRet->targets.front();
    target.targetId     = "person:0";
    target.bLogicResult = false;  // A uniform match does not trigger the no-uniform rule.
    input->pictureDecisions[target.targetId] = "not_matched";
    target.matchInfo.setPicCount             = 2;
    target.matchInfo.matched                 = true;
    target.matchInfo.match_id                = "reference-1";
    target.matchInfo.name                    = "Reference name";
    target.matchInfo.group_id                = "library-1";
    target.matchInfo.group_name              = "Library name";
    target.matchInfo.base_image_url          = "/reference.jpg";
    target.matchInfo.person_id               = "person-1";
    target.matchInfo.person_code             = "code-1";
    target.matchInfo.match_degree            = 87.25F;
    MsgPTaskDetectPicRecv request;
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    REQUIRE(runtime.ExecutePicture(task, input, request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(response.resData.outputs.front().decision == "not_matched");
    REQUIRE(response.resData.outputs.front().matchedCount == 0);
    REQUIRE(response.resData.outputs.front().targetIds.empty());
    REQUIRE(rendered->chanDataDetect.detRet->targets.empty());
    REQUIRE(response.resData.targetList.size() == 1);
    nlohmann::json json = response;
    const auto& match   = json["resData"]["targetList"][0]["matchInfo"];
    CHECK(match["matched"] == true);
    CHECK(match["matchId"] == "reference-1");
    CHECK(match["name"] == "Reference name");
    CHECK(match["groupName"] == "Library name");
    CHECK(match["baseImageUrl"] == "/reference.jpg");
    CHECK(match["personId"] == "person-1");
    CHECK(match["personCode"] == "code-1");
    CHECK(match["matchDegree"] == 87.25);
    const auto restored = json.get<MsgPTaskDetectPicSend>();
    CHECK(restored.resData.targetList.front().matchInfo.name == "Reference name");
    CHECK(restored.resData.targetList.front().matchInfo.baseImageUrl == "/reference.jpg");
    // Existing clients' payloads without the new optional fields remain valid.
    nlohmann::json legacy = {{"matched", true}, {"matchDegree", 87.25}};
    CHECK(legacy.get<MsgMatchInfo>().name.empty());

    target.bFilter = true;
    REQUIRE(runtime.ExecutePicture(task, input, request, response, rendered) == util::ErrorEnum::Success);
    CHECK(response.resData.targetList.empty());
}

TEST_CASE("Picture unknown results explain unavailable comparison evidence", "[picture][workflow]") {
    PTaskBase runtime;
    auto task = Task();
    AddRule(task, Node("out", PAOutput_Code));
    auto input                               = Input();
    input->bHaveLogic                        = true;
    auto& target                             = input->chanDataDetect.detRet->targets.front();
    target.targetId                          = "person:0";
    target.matchInfo.setPicCount             = GENERATE(0, -1);
    input->pictureDecisions[target.targetId] = "unknown";
    MsgPTaskDetectPicRecv request;
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    REQUIRE(runtime.ExecutePicture(task, input, request, response, rendered) == util::ErrorEnum::Success);
    REQUIRE(response.resData.outputs.front().decision == "unknown");
    CHECK(response.resData.targetList.empty());
    const std::string reason =
        target.matchInfo.setPicCount == 0 ? "no_comparable_samples" : "insufficient_evidence";
    nlohmann::json json = response;
    CHECK(json["resData"]["outputs"][0]["reason"] == reason);
    CHECK(json.get<MsgPTaskDetectPicSend>().resData.outputs.front().reason == reason);
}
