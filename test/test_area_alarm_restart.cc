#include "catch_amalgamated.hpp"

#define private public
#include "flow/alarm/AreaAlarm.h"
#undef private

#include "flow/alarm/AreaAlarmInternalTypes.h"

using namespace cosmo;

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
