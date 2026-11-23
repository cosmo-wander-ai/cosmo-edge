#include <nlohmann/json.hpp>

#include "catch_amalgamated.hpp"

#define private public
#include "flow/alarm/AreaAlarm.h"
#undef private

#include "flow/alarm/AreaAlarmInternalTypes.h"
#include "flow/alarm/AreaAlarmMng.h"

using namespace cosmo;

TEST_CASE("Area presence uses the current frame while dwell uses its channel duration",
          "[AreaAlarm][task-parameters][presence]") {
    const auto parameter = [](const std::string& key, const std::string& value) {
        const auto parts = util::Split(key, ".");
        return MsgDynamicKeyValue{key, value, {parts.begin(), parts.end()}, {}};
    };
    MsgTaskArea area;
    area.areaId = "main";
    std::vector<MsgTaskArea> areas{area}, shieldedAreas;
    std::vector<MsgDynamicKeyValue> channelParams{parameter("param.areaDuration", "20"),
                                                  parameter("param.areaDurationTimeType", "1000")};
    auto frame                   = std::make_shared<AlgData>();
    frame->dataType              = AlgDataType::ChannelDataDetect;
    frame->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    auto& targets                = frame->chanDataDetect.detRet->targets;
    TargetAreaUnit inside;
    inside.area_id = area.areaId;
    targets.resize(4);
    for (int index = 0; index < 4; ++index)
        targets[index].trackId = index + 1;
    targets[0].areaSign.areas.push_back(inside);
    targets[1].areaSign.areas.push_back(inside);
    targets[1].areaSign.shielded_areas.push_back(inside);
    targets[2].areaSign.areas.push_back(inside);
    targets[2].bFilter = true;

    ActionNode action;
    const auto applyFirstFrame = [&](AreaAlarm& alarm) {
        alarm.ModifyParam("channel", "task", channelParams);
        REQUIRE(alarm.SetArea("channel", "task", areas, shieldedAreas));
        alarm.HandFrame(frame);
    };

    SECTION("presence ignores a persisted crossing selector and a channel-wide dwell time") {
        action.configObject.params = {parameter("areaAlarmType", "5"), parameter("detectBreakAreaType", "0"),
                                      parameter("breakAreaType", "101")};
        AreaAlarm alarm("task", action);
        applyFirstFrame(alarm);
    }

    SECTION("dwell still requires the configured time inside the area") {
        action.configObject.params = {parameter("areaAlarmType", "4"),
                                      parameter("durationBreakAreaType", "0")};
        AreaAlarm alarm("task", action);
        applyFirstFrame(alarm);
        REQUIRE_FALSE(frame->taskDataAlarm.alarmData);
        frame->firstTimePoint += std::chrono::seconds(21);
        alarm.HandFrame(frame);
    }

    REQUIRE(frame->taskDataAlarm.alarmData);
    const auto& alarms = frame->taskDataAlarm.alarmData->alarms;
    REQUIRE(alarms.size() == 1);
    CHECK(alarms.front().areaId == area.areaId);
    CHECK(alarms.front().trackId == 1);
}

TEST_CASE("Area alarm manager uses concrete flow metadata for lookup and deletion", "[AreaAlarm]") {
    ActionNode action;
    action.flowActionId = "action-flow";
    AreaAlarmMng manager;
    auto alarm = manager.GetInst("task", action);
    REQUIRE(alarm);
    alarm->action_info_.flowActionId = "alarm-flow";
    CHECK(static_cast<const AlgActionBase&>(*alarm).GetFlowActionId() == "action-flow");
    CHECK(alarm->GetAlarmFlowActionId() == "alarm-flow");

    action.flowActionId = "alarm-flow";
    CHECK(manager.GetInst("task", action) == alarm);
    CHECK(manager.DeleteInst(alarm));
    CHECK(manager.GetInst("task", action) != alarm);
}

TEST_CASE("AreaAlarm stop-start clears runtime history and rebuilds configured areas",
          "[AreaAlarm][task-parameters][restart]") {
    ActionNode action;
    action.actionName   = "area-alarm";
    action.flowActionId = "area-flow";
    AreaAlarm alarm("restart-task", action);

    MsgTaskArea area;
    area.areaId = "area-1";
    area.name   = "Area 1";
    std::vector<MsgTaskArea> areas{area};
    std::vector<MsgTaskArea> shielded_areas;
    REQUIRE(alarm.SetArea("channel", "restart-task", areas, shielded_areas));

    REQUIRE(alarm.area_target_status_map_.size() == 1);
    REQUIRE(alarm.pass_flow_areas_map_.size() == 1);

    // Exercise the same lifecycle path used by TaskStop/TaskStart. The first
    // Start uses the original queue; after Stop, the next Start recreates the
    // queue and invokes ResetStateOnRestart().
    REQUIRE(alarm.Start());
    alarm.Stop();

    auto& target_status        = alarm.area_target_status_map_.at("area-1");
    target_status.target_count = 3;
    target_status.history_full = true;
    target_status.history.emplace_back();

    auto& pass_status           = alarm.pass_flow_areas_map_.at("area-1");
    pass_status.hour            = 2026090101;
    pass_status.enter_number    = 2;
    pass_status.leave_number    = 3;
    pass_status.enter_org_num   = 4;
    pass_status.leave_org_num   = 5;
    pass_status.enter_total_num = 6;
    pass_status.leave_total_num = 7;
    pass_status.target_map.emplace(42, AreaAlarm::PassFlowTrackIdData{});
    alarm.track_id_status_map_.emplace(42, AreaAlarm::TrackIdData{});
    alarm.area_target_status_map_["stale"].area_id = "stale";
    alarm.pass_flow_areas_map_["stale"].area_id    = "stale";

    REQUIRE(alarm.Start());
    alarm.Stop();

    REQUIRE(alarm.track_id_status_map_.empty());
    REQUIRE(alarm.area_target_status_map_.size() == 1);
    REQUIRE(alarm.pass_flow_areas_map_.size() == 1);

    const auto& reset_target = alarm.area_target_status_map_.at("area-1");
    CHECK(reset_target.area_id == "area-1");
    CHECK(reset_target.area_name == "Area 1");
    CHECK(reset_target.target_count == 0);
    CHECK_FALSE(reset_target.history_full);
    CHECK(reset_target.history.empty());

    const auto& reset_pass = alarm.pass_flow_areas_map_.at("area-1");
    CHECK(reset_pass.area_id == "area-1");
    CHECK(reset_pass.area_name == "Area 1");
    CHECK(reset_pass.hour == 0);
    CHECK(reset_pass.enter_number == 0);
    CHECK(reset_pass.leave_number == 0);
    CHECK(reset_pass.enter_org_num == 0);
    CHECK(reset_pass.leave_org_num == 0);
    CHECK(reset_pass.enter_total_num == 0);
    CHECK(reset_pass.leave_total_num == 0);
    CHECK(reset_pass.target_map.empty());
}
