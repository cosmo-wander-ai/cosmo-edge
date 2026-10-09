#include <deque>
#include <string>
#include <utility>

#include "catch_amalgamated.hpp"
#include "flow/alarm/AlarmBatch.h"
#include "service/ai/IVisualDecisionService.h"

namespace {

cosmo::DataAlarmUnit MakeAlarm(int trackId, std::string trackIdText, std::string flowActionId,
                               std::string areaId, cosmo::util::Box box) {
    cosmo::DataAlarmUnit unit;
    unit.flowActionId = std::move(flowActionId);
    unit.areaId       = std::move(areaId);
    unit.areaName     = "test-area";
    unit.reportType   = cosmo::OnEventsReportType::Trigger;
    unit.trackId      = trackId;
    unit.strTrackId   = trackIdText;
    unit.box          = box;
    unit.boxs.push_back(box);

    cosmo::CMsgOnEventsTarget target;
    target.label      = "no-helmet";
    target.confidence = 0.9F;
    target.trackId    = std::move(trackIdText);
    target.box.x      = box.x;
    target.box.y      = box.y;
    target.box.width  = box.width;
    target.box.height = box.height;
    unit.targets.push_back(std::move(target));
    return unit;
}

}  // namespace

TEST_CASE("ordinary same-frame target alarms form one deterministic batch", "[alarm][batch]") {
    std::deque<cosmo::DataAlarmUnit> alarms;
    alarms.push_back(MakeAlarm(9, "track-9", "flow-1", "area-1", {300, 80, 60, 160}));
    alarms.push_back(MakeAlarm(4, "track-4", "flow-1", "area-1", {100, 60, 80, 180}));

    const auto batches = cosmo::alarm::BuildAlarmBatches(alarms, cosmo::OnEventsPropertyType::None, 1);
    REQUIRE(batches.size() == 1);
    REQUIRE(batches[0].size() == 2);
    CHECK(batches[0][0] == 1);
    CHECK(batches[0][1] == 0);

    const auto merged = cosmo::alarm::MergeAlarmBatch(alarms, batches[0]);
    REQUIRE(merged.targets.size() == 2);
    REQUIRE(merged.boxs.size() == 2);
    CHECK(merged.trackId == 4);
    CHECK(merged.strTrackId == "track-4");
    CHECK(merged.targets[0].trackId == "track-4");
    CHECK(merged.targets[1].trackId == "track-9");
    CHECK(merged.box == (cosmo::util::Box{100, 60, 260, 180}));
}

TEST_CASE("untracked same-frame target alarms batch any number of targets", "[alarm][batch][untracked]") {
    std::deque<cosmo::DataAlarmUnit> alarms;
    alarms.push_back(MakeAlarm(-1, "", "flow-1", "area-1", {100, 60, 80, 180}));
    alarms.push_back(MakeAlarm(-1, "", "flow-1", "area-1", {300, 80, 60, 160}));
    alarms.push_back(MakeAlarm(-1, "", "flow-1", "area-1", {500, 100, 70, 150}));

    const auto batches = cosmo::alarm::BuildAlarmBatches(alarms, cosmo::OnEventsPropertyType::None, 1);
    REQUIRE(batches.size() == 1);
    REQUIRE(batches[0].size() == 3);
    CHECK(batches[0] == cosmo::alarm::AlarmBatchIndices{0, 1, 2});

    const auto merged = cosmo::alarm::MergeAlarmBatch(alarms, batches[0]);
    REQUIRE(merged.targets.size() == 3);
    REQUIRE(merged.boxs.size() == 3);
    CHECK(merged.trackId == -1);
    CHECK(merged.strTrackId.empty());
    CHECK(merged.box == (cosmo::util::Box{100, 60, 470, 190}));
}

TEST_CASE("alarm batching preserves semantic boundaries", "[alarm][batch]") {
    auto first                 = MakeAlarm(1, "track-1", "flow-1", "area-1", {0, 0, 50, 50});
    auto differentArea         = MakeAlarm(2, "track-2", "flow-1", "area-2", {60, 0, 50, 50});
    auto differentFlow         = MakeAlarm(3, "track-3", "flow-2", "area-1", {120, 0, 50, 50});
    auto differentReport       = MakeAlarm(4, "track-4", "flow-1", "area-1", {180, 0, 50, 50});
    differentReport.reportType = cosmo::OnEventsReportType::Realtime;
    std::deque<cosmo::DataAlarmUnit> alarms{first, differentArea, differentFlow, differentReport};

    CHECK(cosmo::alarm::BuildAlarmBatches(alarms, cosmo::OnEventsPropertyType::None, 1).size() == 4);

    std::deque<cosmo::DataAlarmUnit> sameKey{first, first};
    CHECK(cosmo::alarm::BuildAlarmBatches(sameKey, cosmo::OnEventsPropertyType::Face, 1).size() == 2);
    CHECK(cosmo::alarm::BuildAlarmBatches(sameKey, cosmo::OnEventsPropertyType::None, 2).size() == 2);
}

TEST_CASE("alarm batch merge keeps only accepted target members", "[alarm][batch]") {
    std::deque<cosmo::DataAlarmUnit> alarms;
    alarms.push_back(MakeAlarm(1, "track-1", "flow-1", "area-1", {10, 10, 40, 60}));
    alarms.push_back(MakeAlarm(2, "track-2", "flow-1", "area-1", {100, 20, 50, 70}));
    alarms.push_back(MakeAlarm(3, "track-3", "flow-1", "area-1", {200, 30, 60, 80}));

    const cosmo::alarm::AlarmBatchIndices accepted{0, 2};
    const auto merged = cosmo::alarm::MergeAlarmBatch(alarms, accepted);
    REQUIRE(merged.targets.size() == 2);
    CHECK(merged.targets[0].trackId == "track-1");
    CHECK(merged.targets[1].trackId == "track-3");
    CHECK(merged.box == (cosmo::util::Box{10, 10, 250, 100}));
}

TEST_CASE("Visual alarm batches never combine configuration epochs", "[visual-flow][batch]") {
    auto a      = MakeAlarm(1, "a", "flow", "area", {0, 0, 10, 10});
    auto b      = MakeAlarm(2, "b", "flow", "area", {10, 0, 10, 10});
    a.visualRun = std::make_shared<cosmo::service::VisualDecisionRun>("task", "old", "rev-1");
    b.visualRun = std::make_shared<cosmo::service::VisualDecisionRun>("task", "new", "rev-2");
    CHECK(cosmo::alarm::BuildAlarmBatches({a, b}, cosmo::OnEventsPropertyType::None, 1).size() == 2);
    b.visualRun = a.visualRun;
    CHECK(cosmo::alarm::BuildAlarmBatches({a, b}, cosmo::OnEventsPropertyType::None, 1).size() == 1);
}
