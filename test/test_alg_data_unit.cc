#include "catch_amalgamated.hpp"
/*
 * test_alg_data_unit.cc - AlgDataUnit (AlgDataCopy, GetAreaOsdLines) unit tests
 */
#include "flow/common/AlgDataRecord.h"
#include "flow/common/AlgDataUnit.h"
#include "flow/common/AreaLineUtil.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"

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

namespace {

std::vector<DataDetTrackClassifyPtr> DetectionResults(const AlgData& data) {
    std::vector<DataDetTrackClassifyPtr> results{data.legacyDetect.detRet, data.chanDataDetect.detRet,
                                                 data.taskAiFilter.targetRst,
                                                 data.taskDataClassifyMultPic.classifyRst};
    for (int type = static_cast<int>(AlgDataType::TaskDataTrack);
         type <= static_cast<int>(AlgDataType::TaskDataClassifyMultPic); ++type) {
        results.push_back(data.GetTaskResult(static_cast<AlgDataType>(type)));
    }
    return results;
}

void SetDetectionResults(AlgData& data, const DataDetTrackClassifyPtr& result) {
    data.legacyDetect.detRet                 = result;
    data.chanDataDetect.detRet               = result;
    data.taskAiFilter.targetRst              = result;
    data.taskDataClassifyMultPic.classifyRst = result;
    for (int type = static_cast<int>(AlgDataType::TaskDataTrack);
         type <= static_cast<int>(AlgDataType::TaskDataClassifyMultPic); ++type) {
        data.SetTaskResult(static_cast<AlgDataType>(type), result);
    }
}

}  // namespace

TEST_CASE("AlgDataCopy: preserves multi-related flag at every detection location",
          "[AlgDataUnit][LeafCopy]") {
    const bool multi_related      = GENERATE(false, true);
    auto src                      = std::make_shared<AlgData>();
    auto result                   = std::make_shared<DataDetTrackClassify>();
    result->targetHaveMultRelated = multi_related;
    SetDetectionResults(*src, result);

    auto copy = AlgDataCopy(src);
    for (const auto& copied : DetectionResults(*copy)) {
        REQUIRE(copied);
        CHECK(copied->targetHaveMultRelated == multi_related);
    }
}

TEST_CASE("AlgDataCopy: branches own separate leaves and nested values", "[AlgDataUnit][LeafCopy]") {
    auto src    = std::make_shared<AlgData>();
    auto result = std::make_shared<DataDetTrackClassify>();
    AiDetectRstEl target;
    target.targetId = "original";
    target.relatedEls.resize(1);
    target.relatedEls[0].classifyRst.resize(1);
    target.relatedEls[0].classifyRst[0].label = "nested";
    result->targets.push_back(target);
    AiGroupEl group;
    group.groupId = 7;
    group.srcTargets.push_back(target);
    result->groupTargets.push_back(group);
    SetDetectionResults(*src, result);
    src->taskDataAlarm.alarmData               = std::make_shared<DataAlarm>();
    src->taskDataAlarm.alarmData->flowActionId = "alarm-action";
    src->taskDataAlarm.alarmData->multiAlarms  = 2;
    src->taskDataAlarm.alarmData->alarms.emplace_back();
    src->taskDataAlarm.alarmData->alarms[0].targetHistory.emplace_back();
    src->taskDataAlarm.alarmData->alarms[0].targetHistory[0].friends.resize(1);
    src->taskDataAlarm.alarmData->alarms[0].targetHistory[0].friends[0].width = 12;

    auto first          = AlgDataCopy(src);
    auto second         = AlgDataCopy(src);
    auto first_results  = DetectionResults(*first);
    auto second_results = DetectionResults(*second);
    REQUIRE(first_results.size() == second_results.size());
    for (size_t i = 0; i < first_results.size(); ++i) {
        CAPTURE(i);
        REQUIRE(first_results[i]);
        REQUIRE(second_results[i]);
        CHECK(first_results[i] != result);
        CHECK(second_results[i] != result);
        CHECK(first_results[i] != second_results[i]);
        for (size_t j = 0; j < i; ++j) {
            CHECK(first_results[i] != first_results[j]);
        }
        first_results[i]->targets[0].targetId                           = "changed";
        first_results[i]->targets[0].relatedEls[0].classifyRst[0].label = "changed";
        first_results[i]->groupTargets[0].groupId                       = 99;
        first_results[i]->groupTargets[0].srcTargets[0].targetId        = "changed";
        CHECK(second_results[i]->targets[0].targetId == "original");
        CHECK(second_results[i]->targets[0].relatedEls[0].classifyRst[0].label == "nested");
        CHECK(second_results[i]->groupTargets[0].groupId == 7);
        CHECK(second_results[i]->groupTargets[0].srcTargets[0].targetId == "original");
    }
    CHECK(result->targets[0].targetId == "original");
    CHECK(result->targets[0].relatedEls[0].classifyRst[0].label == "nested");
    CHECK(result->groupTargets[0].groupId == 7);
    CHECK(result->groupTargets[0].srcTargets[0].targetId == "original");
    REQUIRE(first->taskDataAlarm.alarmData != src->taskDataAlarm.alarmData);
    REQUIRE(second->taskDataAlarm.alarmData != src->taskDataAlarm.alarmData);
    REQUIRE(first->taskDataAlarm.alarmData != second->taskDataAlarm.alarmData);
    CHECK(first->taskDataAlarm.alarmData->flowActionId == "alarm-action");
    CHECK(first->taskDataAlarm.alarmData->multiAlarms == 2);
    first->taskDataAlarm.alarmData->alarms[0].targetHistory[0].friends[0].width = 99;
    CHECK(src->taskDataAlarm.alarmData->alarms[0].targetHistory[0].friends[0].width == 12);
    CHECK(second->taskDataAlarm.alarmData->alarms[0].targetHistory[0].friends[0].width == 12);
    first->taskDataAlarm.alarmData->alarms.clear();
    CHECK(src->taskDataAlarm.alarmData->alarms.size() == 1);
    CHECK(second->taskDataAlarm.alarmData->alarms.size() == 1);
}

TEST_CASE("AlgDataCopy: preserves null leaves, empty containers and absent keys", "[AlgDataUnit][LeafCopy]") {
    auto src  = std::make_shared<AlgData>();
    auto copy = AlgDataCopy(src);
    CHECK(copy->taskResults.empty());
    for (const auto& result : DetectionResults(*copy)) {
        CHECK_FALSE(result);
    }
    CHECK_FALSE(copy->taskDataAlarm.alarmData);
    CHECK_FALSE(copy->chanDataOrig.packet);
    CHECK_FALSE(copy->chanDataDec.frame);
    CHECK_FALSE(copy->chanDataDec.native_buffer);
    CHECK_FALSE(copy->taskDataClassifyMultPic.baseFrame);

    src->SetTaskResult(AlgDataType::TaskDataTrack, nullptr);
    src->SetTaskResult(AlgDataType::TaskDataClassify, std::make_shared<DataDetTrackClassify>());
    src->taskDataAlarm.alarmData = std::make_shared<DataAlarm>();
    copy                         = AlgDataCopy(src);
    CHECK(copy->taskResults.size() == 2);
    CHECK(copy->taskResults.count(AlgDataType::TaskDataTrack) == 1);
    CHECK_FALSE(copy->GetTaskResult(AlgDataType::TaskDataTrack));
    CHECK(copy->taskResults.count(AlgDataType::TaskDataOcr) == 0);
    REQUIRE(copy->GetTaskResult(AlgDataType::TaskDataClassify));
    CHECK(copy->GetTaskResult(AlgDataType::TaskDataClassify)->targets.empty());
    CHECK(copy->GetTaskResult(AlgDataType::TaskDataClassify)->groupTargets.empty());
    REQUIRE(copy->taskDataAlarm.alarmData);
    CHECK(copy->taskDataAlarm.alarmData->alarms.empty());
}

TEST_CASE("AlgDataCopy: shares frame, packet and native owners until the last branch",
          "[AlgDataUnit][LeafCopy]") {
    constexpr int pool_size = 4 * 4 * 3 / 2;
    mem::MemoryPoolMng memory_pool(std::make_unique<mem::AllocatorCpu>(), {pool_size});
    mem::SetMemoryPoolContext(&memory_pool);
    struct PoolReset {
        ~PoolReset() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } pool_reset;
    auto src      = std::make_shared<AlgData>();
    auto frame    = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_I420);
    auto packet   = std::make_shared<media::VideoPacket>();
    auto owner    = std::make_shared<int>(42);
    auto native   = std::make_shared<media::NativeVideoBuffer>();
    native->owner = owner;
    std::weak_ptr<media::VideoFrame> weak_frame   = frame;
    std::weak_ptr<media::VideoPacket> weak_packet = packet;
    std::weak_ptr<int> weak_owner                 = owner;
    src->chanDataOrig.packet                      = packet;
    src->chanDataDec.frame                        = frame;
    src->chanDataDec.native_buffer                = native;
    src->taskDataClassifyMultPic.baseFrame        = frame;
    auto result                                   = std::make_shared<DataDetTrackClassify>();
    result->areaInfo.intoAreaFrame                = frame;
    result->areaInfo.outAreaFrame                 = frame;
    result->targets.resize(1);
    result->targets[0].bestEl.bestFrame = frame;
    result->groupTargets.resize(1);
    result->groupTargets[0].srcTargets = result->targets;
    SetDetectionResults(*src, result);
    auto alarm = std::make_shared<DataAlarm>();
    alarm->alarms.emplace_back();
    alarm->alarms[0].ocrImage  = frame;
    alarm->alarms[0].baseFrame = frame;
    alarm->alarms[0].bestInfos.push_back(result->targets[0].bestEl);
    alarm->alarms[0].areaInfo    = result->areaInfo;
    src->taskDataAlarm.alarmData = alarm;

    auto first  = AlgDataCopy(src);
    auto second = AlgDataCopy(src);
    for (const auto& branch : {first, second}) {
        CHECK(branch->chanDataOrig.packet == packet);
        CHECK(branch->chanDataDec.frame == frame);
        CHECK(branch->chanDataDec.native_buffer == native);
        CHECK(branch->chanDataDec.native_buffer->owner == owner);
        CHECK(branch->taskDataClassifyMultPic.baseFrame == frame);
        for (const auto& copied : DetectionResults(*branch)) {
            CHECK(copied->areaInfo.intoAreaFrame == frame);
            CHECK(copied->areaInfo.outAreaFrame == frame);
            CHECK(copied->targets[0].bestEl.bestFrame == frame);
            CHECK(copied->groupTargets[0].srcTargets[0].bestEl.bestFrame == frame);
        }
        const auto& copied_alarm = branch->taskDataAlarm.alarmData->alarms[0];
        CHECK(copied_alarm.ocrImage == frame);
        CHECK(copied_alarm.baseFrame == frame);
        CHECK(copied_alarm.bestInfos[0].bestFrame == frame);
        CHECK(copied_alarm.areaInfo.intoAreaFrame == frame);
        CHECK(copied_alarm.areaInfo.outAreaFrame == frame);
    }
    result.reset();
    alarm.reset();
    frame.reset();
    packet.reset();
    owner.reset();
    native.reset();
    src.reset();
    first.reset();
    CHECK_FALSE(weak_frame.expired());
    CHECK_FALSE(weak_packet.expired());
    CHECK_FALSE(weak_owner.expired());
    second.reset();
    CHECK(weak_frame.expired());
    CHECK(weak_packet.expired());
    CHECK(weak_owner.expired());
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
