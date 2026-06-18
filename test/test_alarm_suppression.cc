#include "catch_amalgamated.hpp"
/*
 * test_alarm_suppression.cc - TaskAlarmSuppression unit tests
 */
#include "flow/alarm/TaskAlarmSuppression.h"

using namespace cosmo;

TEST_CASE("TaskAlarmSuppression: Disabled always reports", "[AlarmSuppression]") {
    TaskAlarmSuppression s("test_dis");
    util::Box b{100, 100, 200, 200};
    REQUIRE(s.CheckReportAlarm(b, false, 0.5f, 3600));
    REQUIRE(s.CheckReportAlarm(b, false, 0.5f, 3600));
}

TEST_CASE("TaskAlarmSuppression: First target reports", "[AlarmSuppression]") {
    TaskAlarmSuppression s("test_first");
    util::Box b{100, 100, 200, 200};
    REQUIRE(s.CheckReportAlarm(b, true, 0.5f, 3600));
}

TEST_CASE("TaskAlarmSuppression: IOU<=0 reports", "[AlarmSuppression]") {
    TaskAlarmSuppression s("test_iou0");
    util::Box b{100, 100, 200, 200};
    REQUIRE(s.CheckReportAlarm(b, true, 0.0f, 3600));
    REQUIRE(s.CheckReportAlarm(b, true, -1.0f, 3600));
}

TEST_CASE("TaskAlarmSuppression: Non-overlapping reports", "[AlarmSuppression]") {
    TaskAlarmSuppression s("test_no_ol");
    util::Box b1{0, 0, 100, 100};
    REQUIRE(s.CheckReportAlarm(b1, true, 0.5f, 3600));
    util::Box b2{500, 500, 100, 100};
    REQUIRE(s.CheckReportAlarm(b2, true, 0.5f, 3600));
}

TEST_CASE("TaskAlarmSuppression: Same pos suppressed", "[AlarmSuppression]") {
    TaskAlarmSuppression s("test_same");
    util::Box b{100, 100, 200, 200};
    REQUIRE(s.CheckReportAlarm(b, true, 0.3f, 99999));
    REQUIRE_FALSE(s.CheckReportAlarm(b, true, 0.3f, 99999));
}

TEST_CASE("TaskAlarmSuppression: Toggle resets", "[AlarmSuppression]") {
    TaskAlarmSuppression s("test_tog");
    util::Box b{100, 100, 200, 200};
    REQUIRE(s.CheckReportAlarm(b, true, 0.5f, 99999));
    REQUIRE_FALSE(s.CheckReportAlarm(b, true, 0.5f, 99999));
    REQUIRE(s.CheckReportAlarm(b, false, 0.5f, 99999));
    REQUIRE(s.CheckReportAlarm(b, true, 0.5f, 99999));
}

TEST_CASE("TaskAlarmSuppression: Zero area safe", "[AlarmSuppression]") {
    TaskAlarmSuppression s("test_zero");
    util::Box b{100, 100, 0, 0};
    REQUIRE(s.CheckReportAlarm(b, true, 0.5f, 3600));
}
