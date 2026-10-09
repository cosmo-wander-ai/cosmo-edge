#include "catch_amalgamated.hpp"
#include "flow/face/Person.h"
#include "flow/recognizer/PFaceCompare.h"
#include "mock/MockFaceLibService.h"
#include "nlohmann/json.hpp"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {
void DetData2MsgData(const std::vector<MsgTaskArea>&, DataDetTrackClassifyPtr, std::vector<MsgPTaskArea>&,
                     std::vector<MsgPTaskTarget>&, bool);
}

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

TEST_CASE("Picture face comparison validates libraries and preserves unknown faces", "[picture-face]") {
    struct {
        MockFaceLibService faceLibSvc;
    } mocks;
    auto library = std::make_shared<FaceLib>(std::string{});
    auto person  = std::make_shared<Person>("test-person");
    auto pic     = std::make_shared<FacePic>("test-photo", "", AiFeature{{1.0F, 0.0F}});
    person->AddFaceLib(library);
    std::vector<AiDetectRstEl> targets(2);
    targets[0].feature.feature = {1, 0};
    targets[1].feature.feature = {0, 1};
    auto compare               = [&](std::vector<std::string> ids, float threshold = 80) {
        return ComparePictureFaces(targets, ids, threshold, mocks.faceLibSvc, mocks.faceLibSvc);
    };
    SECTION("missing selection") {
        CHECK(compare({}) == util::ErrorEnum::InvalidParam);
    }
    SECTION("unknown library") {
        REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("missing")).RETURN(FaceLibPtr{});
        CHECK(compare({"missing"}) == util::ErrorEnum::NoSuchId);
    }
    SECTION("empty library") {
        REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("empty")).RETURN(library);
        CHECK(compare({"empty"}) == util::ErrorEnum::DependLibEmpty);
    }
    SECTION("matched and unknown targets both remain in the result") {
        person->AddPicture(pic);
        REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("selected")).RETURN(library);
        trompeloeil::sequence order;
        REQUIRE_CALL(mocks.faceLibSvc, FaceCompare(std::vector<std::string>{"selected"}, _, _, 80))
            .IN_SEQUENCE(order)
            .SIDE_EFFECT(_3.match_degree = 95)
            .SIDE_EFFECT(_3.name = "Known")
            .RETURN(true);
        REQUIRE_CALL(mocks.faceLibSvc, FaceCompare(std::vector<std::string>{"selected"}, _, _, 80))
            .IN_SEQUENCE(order)
            .SIDE_EFFECT(_3.match_degree = 40)
            .RETURN(false);
        CHECK(compare({"selected"}) == util::ErrorEnum::Success);
        CHECK(targets.size() == 2);
        CHECK(targets[0].matchInfo.matched);
        CHECK_FALSE(targets[1].matchInfo.matched);
        CHECK(targets[1].matchInfo.setPicCount == 1);
    }
    SECTION("empty features are an extraction error, not an unknown person") {
        person->AddPicture(pic);
        targets[0].feature.feature.clear();
        REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("selected")).RETURN(library);
        FORBID_CALL(mocks.faceLibSvc, FaceCompare(_, _, _, _));
        CHECK(compare({"selected"}) == util::ErrorEnum::GetFeatureFailed);
    }
}

TEST_CASE("Picture matching serializes optional identity without breaking legacy results", "[picture-face]") {
    MsgPTaskTarget target;
    target.bHaveMatchInfo         = true;
    target.matchInfo.matched      = true;
    target.matchInfo.personId     = "p1";
    target.matchInfo.personName   = "Example";
    target.matchInfo.personCode   = "E001";
    target.matchInfo.baseImageUrl = "/example.jpg";
    nlohmann::json json           = target;
    auto restored                 = json.get<MsgPTaskTarget>();
    CHECK(restored.matchInfo.personName == "Example");
    CHECK(restored.matchInfo.personCode == "E001");
    auto legacy = nlohmann::json{{"matched", false}, {"matchDegree", 30}}.get<MsgMatchInfo>();
    CHECK(legacy.personName.empty());
    target.bHaveMatchInfo = false;
    json                  = target;
    CHECK_FALSE(json.contains("matchInfo"));
}

TEST_CASE("Picture response includes unknown faces without leaking candidate identity", "[picture-face]") {
    auto frame = std::make_shared<DataDetTrackClassify>();
    frame->targets.resize(2);
    for (auto& target : frame->targets) {
        target.matchInfo.setPicCount  = 2;
        target.matchInfo.match_degree = 45;
        target.matchInfo.name         = "Candidate";
        target.matchInfo.person_id    = "p1";
    }
    frame->targets[0].matchInfo.matched      = true;
    frame->targets[0].matchInfo.match_degree = 95;
    std::vector<MsgPTaskArea> areas;
    std::vector<MsgPTaskTarget> targets;
    DetData2MsgData({}, frame, areas, targets, false);
    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].targetList.size() == 2);
    CHECK(areas[0].targetList[0].matchInfo.personName == "Candidate");
    const auto& unknown = areas[0].targetList[1];
    CHECK(unknown.bHaveMatchInfo);
    CHECK_FALSE(unknown.matchInfo.matched);
    CHECK(unknown.matchInfo.matchDegree == 45);
    nlohmann::json json = unknown;
    CHECK_FALSE(json["matchInfo"].contains("personName"));
    CHECK_FALSE(json["matchInfo"].contains("personId"));
}
