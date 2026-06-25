#include "catch_amalgamated.hpp"
/*
 * test_alg_data_unit.cc - AlgDataUnit (AlgDataCopy, GetAreaOsdLines) unit tests
 */
#include "flow/common/AlgDataRecord.h"
#include "flow/common/AlgDataUnit.h"
#include "mock/MockServiceRegistry.h"

using namespace cosmo;

TEST_CASE("AlgDataCopy: nullptr returns nullptr", "[AlgDataUnit]") {
    REQUIRE(AlgDataCopy(nullptr) == nullptr);
}

TEST_CASE("AlgDataCopy: Deep copies basic fields", "[AlgDataUnit]") {
    auto src           = std::make_shared<AlgData>();
    src->dataType      = AlgDataType::TaskDataTrack;
    src->channelId     = "ch_01";
    src->taskId        = "task_01";
    src->bHaveTrack    = true;
    src->bHaveRelated  = false;
    src->bHaveClassify = true;

    auto copy = AlgDataCopy(src);
    REQUIRE(copy != nullptr);
    REQUIRE(copy.get() != src.get());
    REQUIRE(copy->dataType == AlgDataType::TaskDataTrack);
    REQUIRE(copy->channelId == "ch_01");
    REQUIRE(copy->taskId == "task_01");
    REQUIRE(copy->bHaveTrack == true);
    REQUIRE(copy->bHaveRelated == false);
    REQUIRE(copy->bHaveClassify == true);
}

TEST_CASE("AlgDataCopy: Deep copies taskResults map", "[AlgDataUnit]") {
    auto src         = std::make_shared<AlgData>();
    auto det         = std::make_shared<DataDetTrackClassify>();
    det->streamIndex = 42;
    det->frameIndex  = 100;
    src->SetTaskResult(AlgDataType::TaskDataTrack, det);

    auto copy = AlgDataCopy(src);
    REQUIRE(copy != nullptr);
    auto copyDet = copy->GetTaskResult(AlgDataType::TaskDataTrack);
    REQUIRE(copyDet != nullptr);
    REQUIRE(copyDet.get() != det.get());  // Deep copy, not same ptr
    REQUIRE(copyDet->streamIndex == 42);
    REQUIRE(copyDet->frameIndex == 100);
}

TEST_CASE("AlgData::GetTaskResult: Returns nullptr for missing key", "[AlgDataUnit]") {
    AlgData data;
    REQUIRE(data.GetTaskResult(AlgDataType::TaskDataTrack) == nullptr);
}

TEST_CASE("AlgData::SetTaskResult and GetTaskResult roundtrip", "[AlgDataUnit]") {
    AlgData data;
    auto det       = std::make_shared<DataDetTrackClassify>();
    det->picWidth  = 1920;
    det->picHeight = 1080;
    data.SetTaskResult(AlgDataType::TaskDataClassify, det);

    auto result = data.GetTaskResult(AlgDataType::TaskDataClassify);
    REQUIRE(result != nullptr);
    REQUIRE(result->picWidth == 1920);
    REQUIRE(result->picHeight == 1080);
}

TEST_CASE("GetAreaOsdLines: Empty area returns empty", "[AlgDataUnit]") {
    MsgTaskArea area;
    auto lines = GetAreaOsdLines(area, 1920, 1080);
    REQUIRE(lines.empty());
}

TEST_CASE("GetAreaOsdLines: Polygon area generates edges", "[AlgDataUnit]") {
    MsgTaskArea area;
    // Triangle: (0,0) -> (1,0) -> (0.5,1)
    area.points = {{0.0, 0.0}, {1.0, 0.0}, {0.5, 1.0}};

    auto lines = GetAreaOsdLines(area, 1000, 1000);
    // 3 vertices => 2 edges + 1 closing edge = 3 lines
    REQUIRE(lines.size() == 3);
}

TEST_CASE("GetAreaOsdLines: Two-point area (line segment)", "[AlgDataUnit]") {
    MsgTaskArea area;
    area.points = {{0.0, 0.0}, {1.0, 1.0}};

    auto lines = GetAreaOsdLines(area, 100, 100);
    // 2 points => 1 edge, no closing edge (need >2 for closing)
    REQUIRE(lines.size() == 1);
}

TEST_CASE("GetAreasOsdLines: Multiple areas combined", "[AlgDataUnit]") {
    MsgTaskArea area1;
    area1.points = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};  // Quad

    MsgTaskArea area2;
    area2.points = {{0.0, 0.0}, {1.0, 0.0}, {0.5, 1.0}};  // Triangle

    std::vector<MsgTaskArea> areas = {area1, area2};
    auto lines                     = GetAreasOsdLines(areas, 100, 100);
    // Quad=4 + Triangle=3 = 7
    REQUIRE(lines.size() == 7);
}

TEST_CASE("GenRandomDetBoxs: Generates non-empty targets", "[AlgDataUnit]") {
    cosmo::test::MockServiceRegistry mocks;

    auto result = GenRandomDetBoxs();
    REQUIRE(result != nullptr);
    REQUIRE_FALSE(result->targets.empty());
    REQUIRE(result->targets.size() <= 10);

    for (const auto& target : result->targets) {
        REQUIRE(target.box.width >= 32);
        REQUIRE(target.box.height >= 32);
        REQUIRE_FALSE(target.areaSign.areas.empty());
        REQUIRE(target.bFilter == false);
    }
}
