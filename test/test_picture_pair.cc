#include <limits>
#include <nlohmann/json.hpp>

#include "catch_amalgamated.hpp"
#include "flow/logical/PictureRule.h"
#include "flow/recognizer/PPicturePairMatch.h"
#include "flow/task/PTaskBase.h"
#include "util/FeatureComparison.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/PictureWorkflow.h"

using namespace cosmo;
namespace {
MsgDynamicKeyValue PairParam(const std::string& key, const std::string& value) {
    MsgDynamicKeyValue p;
    p.key   = key;
    p.value = value;
    return p;
}
ActionNode PairNode(const std::string& id, std::string_view kind, const std::string& parent) {
    ActionNode n;
    n.flowActionId    = id;
    n.actionId        = kind;
    n.preFlowActionId = parent;
    if (kind == PADetect_Code || kind == PARecognizer_Code)
        n.atomicCode = "shared-model";
    return n;
}
std::vector<ActionNode> PairGraph() {
    auto match                = PairNode("pair", PAPairMatch_Code, "features");
    match.configObject.params = {PairParam("pair.featureType", "face")};
    return {PairNode("det", PADetect_Code, "-1"), PairNode("features", PARecognizer_Code, "det"), match,
            PairNode("out", PAOutput_Code, "pair")};
}
class PassPictureAction : public PActionBase {
public:
    explicit PassPictureAction(ActionNode& n) : PActionBase(n, "pair-test") {}
    util::ErrorEnum HandPic(AlgDataPtr) override {
        return util::ErrorEnum::Success;
    }
};
class CalibratedPair : public PPicturePairMatch {
public:
    explicit CalibratedPair(ActionNode& n) : PPicturePairMatch("pair-test", n) {}
    util::ErrorEnum Compare(const AiFeature& a, const AiFeature& b, double& score) override {
        return util::ComparePictureFeatures(a.feature, b.feature, {0.8F, 1.2F, 1.6F}, true, score);
    }
};
auto PairTask() {
    auto t    = std::make_shared<PTaskElement>();
    t->taskId = "pair-test";
    for (auto n : PairGraph()) {
        PActionBasePtr action;
        if (n.actionId == PAPairMatch_Code)
            action = std::make_shared<CalibratedPair>(n);
        else if (n.actionId == PAOutput_Code)
            action = std::make_shared<PictureRuleAction>("pair-test", n);
        else
            action = std::make_shared<PassPictureAction>(n);
        t->actions.push_back({n, action, {}, nullptr});
    }
    return t;
}
auto PairInput(std::vector<float> feature = {1, 0}, int count = 1) {
    auto d                   = std::make_shared<AlgData>();
    d->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    for (int i = 0; i < count; ++i) {
        AiDetectRstEl target;
        target.box             = util::Box(1, 2, 50, 60);
        target.feature.feature = feature;
        d->chanDataDetect.detRet->targets.push_back(target);
    }
    return d;
}
}  // namespace

TEST_CASE("Pair scoring distinguishes valid zero from invalid features and calibration", "[picture][pair]") {
    double score = -7;
    REQUIRE(util::ComparePictureFeatures({1, 0}, {1, 0}, {0.8F, 1.2F, 1.6F}, true, score) ==
            util::ErrorEnum::Success);
    REQUIRE(score == 100);
    REQUIRE(util::ComparePictureFeatures({1, 0}, {-1, 0}, {0.8F, 1.2F, 1.6F}, true, score) ==
            util::ErrorEnum::Success);
    REQUIRE(score == 0);
    REQUIRE(util::ComparePictureFeatures({1, 0}, {1, 1}, {0.8F, 1.2F, 1.6F}, false, score) ==
            util::ErrorEnum::Success);
    REQUIRE(score == Catch::Approx(70).margin(0.001));
    for (auto invalid : std::vector<std::vector<float>>{{},
                                                        {0, 0},
                                                        {1},
                                                        {std::numeric_limits<float>::quiet_NaN(), 1},
                                                        {1, std::numeric_limits<float>::infinity()}}) {
        score = -7;
        REQUIRE(util::ComparePictureFeatures({1, 0}, invalid, {0.8F, 1.2F, 1.6F}, true, score) ==
                util::ErrorEnum::PicturePairInvalidFeature);
        REQUIRE(score == -7);
    }
    for (auto levels : std::vector<std::vector<float>>{
             {}, {1, 1, 2}, {0, 1, 2}, {2, 1, 3}, {1, 2, std::numeric_limits<float>::infinity()}}) {
        score = -7;
        REQUIRE(util::ComparePictureFeatures({1, 0}, {1, 0}, levels, false, score) ==
                util::ErrorEnum::PicturePairInvalidCalibration);
        REQUIRE(score == -7);
    }
}

TEST_CASE("Pair workflow keeps both targets and low scores and resets optional threshold per request",
          "[picture][pair]") {
    PTaskBase engine;
    auto task = PairTask();
    MsgPTaskDetectPicRecv request;
    request.resultMode        = "debug";
    request.taskConfig.params = {PairParam("pair.threshold", "80")};
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered, reference;
    REQUIRE(engine.ExecutePicture(task, PairInput(), request, response, rendered, PairInput({-1, 0}),
                                  &reference) == util::ErrorEnum::Success);
    REQUIRE(response.resData.comparison.hasScore);
    REQUIRE(response.resData.comparison.score == 0);
    REQUIRE(response.resData.comparison.decision == "not_matched");
    REQUIRE(response.resData.outputs.front().decision == "not_matched");
    REQUIRE(response.resData.targetList.size() == 1);
    REQUIRE(response.resData.referenceTargetList.size() == 1);
    REQUIRE(rendered->chanDataDetect.detRet->targets.size() == 1);
    REQUIRE(reference->chanDataDetect.detRet->targets.size() == 1);
    REQUIRE(response.resData.nodes.size() == 6);
    request.taskConfig.params = {PairParam("pair.threshold", "")};
    REQUIRE(engine.ExecutePicture(task, PairInput(), request, response, rendered, PairInput()) ==
            util::ErrorEnum::Success);
    REQUIRE(response.resData.comparison.score == 100);
    REQUIRE(response.resData.comparison.decision == "score_only");
    nlohmann::json json = response;
    REQUIRE(json["resData"]["comparison"]["score"] == 100);
    REQUIRE_FALSE(json["resData"]["comparison"].contains("threshold"));
}

TEST_CASE("Pair errors identify the input side and never leak the previous score", "[picture][pair]") {
    PTaskBase engine;
    auto task = PairTask();
    MsgPTaskDetectPicRecv request;
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    for (const auto& side : {"A", "B"}) {
        for (int count : {0, 2}) {
            REQUIRE(engine.ExecutePicture(task, PairInput(), request, response, rendered, PairInput()) ==
                    util::ErrorEnum::Success);
            auto a = PairInput(), b = PairInput();
            (std::string(side) == "A" ? a : b) = PairInput({1, 0}, count);
            REQUIRE(
                engine.ExecutePicture(task, a, request, response, rendered, b) ==
                (count ? util::ErrorEnum::PicturePairMultipleTargets : util::ErrorEnum::PicturePairNoTarget));
            REQUIRE(response.resData.errorSide == side);
            REQUIRE(response.resData.status == "failed");
            REQUIRE_FALSE(response.resData.comparison.hasScore);
            nlohmann::json json = response;
            REQUIRE_FALSE(json["resData"].contains("comparison"));
        }
    }
    REQUIRE(engine.ExecutePicture(task, PairInput(), request, response, rendered) ==
            util::ErrorEnum::PicturePairInputRequired);
    request.resultMode = "legacy";
    REQUIRE(engine.ExecutePicture(task, PairInput(), request, response, rendered, PairInput()) ==
            util::ErrorEnum::InvalidParam);
}

TEST_CASE("Pair ignores filtered targets but never accepts missing feature data", "[picture][pair]") {
    PTaskBase engine;
    auto task = PairTask();
    MsgPTaskDetectPicRecv request;
    MsgPTaskDetectPicSend response;
    AlgDataPtr rendered;
    auto peer                                           = PairInput({1, 0}, 2);
    peer->chanDataDetect.detRet->targets.back().bFilter = true;
    REQUIRE(engine.ExecutePicture(task, PairInput(), request, response, rendered, peer) ==
            util::ErrorEnum::Success);
    REQUIRE(response.resData.referenceTargetList.size() == 1);
    REQUIRE(engine.ExecutePicture(task, PairInput(), request, response, rendered, PairInput({})) ==
            util::ErrorEnum::PicturePairInvalidFeature);
    REQUIRE_FALSE(response.resData.comparison.hasScore);
}

TEST_CASE("Pair graphs require a shared linear extraction plan and optional bounded threshold",
          "[picture][pair]") {
    std::string error;
    auto graph = PairGraph();
    REQUIRE(ValidatePictureWorkflow(graph, error));
    auto invalid               = graph;
    invalid[1].preFlowActionId = "-1";
    REQUIRE_FALSE(ValidatePictureWorkflow(invalid, error));
    invalid = graph;
    invalid.insert(invalid.begin() + 2, PairNode("other", PAMatch_Code, "features"));
    REQUIRE_FALSE(ValidatePictureWorkflow(invalid, error));
    invalid             = graph;
    invalid[1].actionId = PAClassify_Code;
    REQUIRE_FALSE(ValidatePictureWorkflow(invalid, error));
    for (auto value : {"", "0", "100", "80.5"})
        REQUIRE(ValidatePictureParams({PairParam("pair.threshold", value)}));
    for (auto value : {"-1", "101", "nan", "invalid"})
        REQUIRE_FALSE(ValidatePictureParams({PairParam("pair.threshold", value)}));
}

TEST_CASE("Pair image request serializes both inputs but never authenticated bytes", "[picture][pair]") {
    MsgPTaskDetectPicRecv request;
    request.algorithmCode            = "pair";
    request.uploadId                 = "first";
    request.referenceImage.uploadId  = "second";
    request.referenceImage.imageData = {1, 2, 3};
    nlohmann::json json              = request;
    REQUIRE(json["referenceImage"]["uploadId"] == "second");
    REQUIRE_FALSE(json["referenceImage"].contains("imageData"));
    auto decoded = json.get<MsgPTaskDetectPicRecv>();
    REQUIRE(decoded.referenceImage.uploadId == "second");
    REQUIRE(decoded.referenceImage.imageData.empty());
}
