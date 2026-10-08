#include <limits>

#include "catch_amalgamated.hpp"
#include "flow/face/FaceManager.h"
#include "flow/recognizer/PPictureMatch.h"
#include "mock/MockFaceLibService.h"
#include "support/ScopedServiceOverride.h"

namespace cosmo::test {
namespace {
    // Forward outside the mock framework lock: the real search scores on worker threads.
    class ForwardingFaceService : public MockFaceLibService {
    public:
        explicit ForwardingFaceService(FaceManager& manager) : manager_(manager) {}
        bool FaceCompare(std::vector<std::string> sets, AiFeature& feature, AiDetectMatchHighScoreInfo& info,
                         float threshold) override {
            return manager_.FaceCompare(std::move(sets), feature, info, threshold);
        }

    private:
        FaceManager& manager_;
    };
}  // namespace
TEST_CASE("Face comparison reports usable samples to picture decisions", "[face-compare][picture]") {
    FaceManager manager;
    ForwardingFaceService feature_service(manager);
    ScopedServiceOverride<service::IFaceFeature> registration{feature_service};
    MsgBaseFaceLibInfo definition;
    definition.name      = "comparison fixture";
    definition.threshold = 80;
    auto library         = std::make_shared<FaceLib>(std::move(definition));
    REQUIRE(manager.AddFaceLib(library) == util::ErrorEnum::Success);
    auto person = std::make_shared<Person>("comparison-person");
    person->SetName("Matched person");
    person->SetSerialNumber("employee-1");
    manager.AddPerson(library, person);
    auto add = [&](const std::string& id, std::vector<float> values) {
        AiFeature feature;
        feature.feature = std::move(values);
        person->AddPicture(std::make_shared<FacePic>(id, "nonexistent-comparison-fixture", feature));
    };
    // Distance ordering is low-first; convert it to similarity for the result.
    ALLOW_CALL(feature_service, CalculateFaceScore(trompeloeil::_, trompeloeil::_))
        .LR_RETURN(_1.feature.front());
    ALLOW_CALL(feature_service, GetFaceScore(trompeloeil::_)).LR_RETURN(100.0F - _1);

    const auto expected =
        GENERATE(std::string("matched"), std::string("not_matched"), std::string("unknown"));
    if (expected != "unknown") {
        add("first", {expected == "matched" ? 5.0F : 60.0F});
        add("second", {90.0F});
    }
    add("empty", {});
    add("wrong-dimension", {1.0F, 2.0F});
    add("invalid-score", {std::numeric_limits<float>::quiet_NaN()});

    ActionNode node;
    node.flowActionId = "face-match";
    node.actionId     = "PB_00006";
    PPictureMatch action("face-test", node);
    auto param = [](const std::string& key, const std::string& value) {
        MsgDynamicKeyValue result;
        result.key   = key;
        result.value = value;
        return result;
    };
    std::vector<MsgDynamicKeyValue> params{param("param.faceSet", library->GetId() + "," + library->GetId()),
                                           param("param.limitScore", "80")};
    REQUIRE(action.SetParam("", params));
    REQUIRE(action.ActionInit());
    auto data                   = std::make_shared<AlgData>();
    data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    AiDetectRstEl target;
    target.targetId        = "face";
    target.feature.feature = {1.0F};
    data->chanDataDetect.detRet->targets.push_back(target);
    REQUIRE(action.HandPic(data) == util::ErrorEnum::Success);
    const auto& result = data->chanDataDetect.detRet->targets.front();
    CHECK(data->pictureDecisions.at("face") == expected);
    CHECK(result.matchInfo.setPicCount == (expected == "unknown" ? 0 : 2));
    CHECK(result.bLogicResult == (expected == "matched"));
    if (expected != "unknown") {
        CHECK(result.matchInfo.match_id == "first");
        CHECK(result.matchInfo.name == "Matched person");
        CHECK(result.matchInfo.person_code == "employee-1");
        CHECK(result.matchInfo.person_id == person->GetId());
        CHECK(result.matchInfo.group_name == "comparison fixture");
        CHECK(result.matchInfo.base_image_url.find("first.jpg") != std::string::npos);
    }

    // Reusing a result with an empty/nonexistent library must clear prior metadata.
    AiDetectMatchHighScoreInfo reused = result.matchInfo;
    reused.matched                    = true;
    AiFeature feature;
    feature.feature = {1.0F};
    CHECK_FALSE(manager.FaceCompare({"missing"}, feature, reused, 80));
    CHECK(reused.setPicCount == 0);
    CHECK_FALSE(reused.matched);
    CHECK(reused.person_id.empty());
}
}  // namespace cosmo::test
