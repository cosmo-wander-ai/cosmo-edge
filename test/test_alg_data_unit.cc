#include "catch_amalgamated.hpp"
/*
 * test_alg_data_unit.cc - AlgDataUnit (AlgDataCopy, GetAreaOsdLines) unit tests
 */
#include "flow/common/AlgDataRecord.h"
#include "flow/common/AlgDataUnit.h"
#include "flow/common/AreaLineUtil.h"
#include "mock/MockServiceRegistry.h"

using namespace cosmo;

namespace {

using AreaLine = std::pair<cosmo::util::Point, cosmo::util::Point>;

AreaLine MakeLine(int x1, int y1, int x2, int y2) {
    return {{x1, y1}, {x2, y2}};
}

void RequireLinesEqual(const std::vector<AreaLine>& actual, const std::vector<AreaLine>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (size_t index = 0; index < expected.size(); ++index) {
        CAPTURE(index);
        REQUIRE(actual[index].first.x == expected[index].first.x);
        REQUIRE(actual[index].first.y == expected[index].first.y);
        REQUIRE(actual[index].second.x == expected[index].second.x);
        REQUIRE(actual[index].second.y == expected[index].second.y);
    }
}

std::vector<AreaLine> GetAreaOsdLinesWithParityCheck(const MsgTaskArea& area, int width, int height) {
    auto osd_lines  = GetAreaOsdLines(area, width, height);
    auto area_lines = GetAreaLines(area, width, height);
    RequireLinesEqual(osd_lines, area_lines);
    return osd_lines;
}

}  // namespace

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
    auto lines = GetAreaOsdLinesWithParityCheck(area, 1920, 1080);
    REQUIRE(lines.empty());
}

TEST_CASE("GetAreaOsdLines: Two-point area preserves its single segment", "[AlgDataUnit]") {
    MsgTaskArea area;
    area.points = {{0.0, 0.0}, {1.0, 1.0}};

    auto lines = GetAreaOsdLinesWithParityCheck(area, 100, 100);
    RequireLinesEqual(lines, {MakeLine(0, 0, 100, 100)});
}

TEST_CASE("GetAreaOsdLines: Polygon area closes its final edge", "[AlgDataUnit]") {
    MsgTaskArea area;
    area.points = {{0.0, 0.0}, {1.0, 0.0}, {0.5, 1.0}};

    auto lines = GetAreaOsdLinesWithParityCheck(area, 100, 100);
    RequireLinesEqual(lines, {MakeLine(0, 0, 100, 0), MakeLine(100, 0, 50, 100), MakeLine(50, 100, 0, 0)});
}

TEST_CASE("GetAreaOsdLines: Associated areas are collected recursively before their parent",
          "[AlgDataUnit]") {
    MsgTaskArea grandchild;
    grandchild.points = {{0.0, 0.0}, {0.5, 0.0}};

    MsgTaskArea child;
    child.points = {{0.0, 0.5}, {0.5, 0.5}};
    child.associatedAreas.push_back(grandchild);

    MsgTaskArea parent;
    parent.points = {{0.0, 1.0}, {0.5, 1.0}};
    parent.associatedAreas.push_back(child);

    auto lines = GetAreaOsdLinesWithParityCheck(parent, 100, 100);
    RequireLinesEqual(lines, {MakeLine(0, 0, 50, 0), MakeLine(0, 50, 50, 50), MakeLine(0, 100, 50, 100)});
}

TEST_CASE("GetAreaOsdLines: One-way line keeps its direction arrow", "[AlgDataUnit]") {
    MsgTaskArea area;
    area.linePoints     = {{0.0, 0.5}, {1.0, 0.5}};
    area.iderectionType = DirectionType::DirectionTypeOneWay;

    auto lines = GetAreaOsdLinesWithParityCheck(area, 100, 100);
    RequireLinesEqual(lines, {MakeLine(0, 50, 100, 50), MakeLine(50, 50, 50, 54), MakeLine(50, 54, 52, 52),
                              MakeLine(50, 54, 48, 52)});
}

TEST_CASE("GetAreaOsdLines: Two-way line keeps both direction arrows", "[AlgDataUnit]") {
    MsgTaskArea area;
    area.linePoints     = {{0.0, 0.5}, {1.0, 0.5}};
    area.iderectionType = DirectionType::DirectionTypeTwoWay;

    auto lines = GetAreaOsdLinesWithParityCheck(area, 100, 100);
    RequireLinesEqual(lines, {MakeLine(0, 50, 100, 50), MakeLine(50, 54, 52, 52), MakeLine(50, 54, 48, 52),
                              MakeLine(50, 54, 50, 46), MakeLine(50, 46, 48, 48), MakeLine(50, 46, 52, 48)});
}

TEST_CASE("GetAreaOsdLines: Zero-length line does not produce segments", "[AlgDataUnit]") {
    MsgTaskArea area;
    area.linePoints = {{0.5, 0.5}, {0.5, 0.5}};

    auto lines = GetAreaOsdLinesWithParityCheck(area, 100, 100);
    REQUIRE(lines.empty());
}

TEST_CASE("GetAreasOsdLines: Multiple areas combined", "[AlgDataUnit]") {
    MsgTaskArea area1;
    area1.points = {{0.0, 0.0}, {1.0, 0.0}};

    MsgTaskArea area2;
    area2.points = {{0.0, 1.0}, {1.0, 1.0}};

    std::vector<MsgTaskArea> areas = {area1, area2};
    auto lines                     = GetAreasOsdLines(areas, 100, 100);
    RequireLinesEqual(lines, {MakeLine(0, 0, 100, 0), MakeLine(0, 100, 100, 100)});
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
