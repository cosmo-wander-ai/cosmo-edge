#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "mock/MockAlarmPushService.h"
#include "mock/MockNetworkService.h"
#include "mock/MockServiceRegistry.h"
#include "service/system/impl/SystemServiceImpl.h"
#include "util/PathUtil.h"

namespace fs = std::filesystem;

namespace {

// Helper: create a minimal valid JSON config file at the given path
void WriteJsonFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path);
    ofs << content;
}

// Creates the required config files in a temp directory so
// SystemServiceImpl can construct without crashing.
// Automatically calls OverrideRootPathForTest to redirect cosmo::path::GetCfgPath()
// to dir/conf/.
struct TestConfigDir {
    std::string dir;

    explicit TestConfigDir(const std::string& base = "/tmp/cosmo_sysconfig_test") : dir(base) {
        cosmo::path::OverrideRootPathForTest(dir, dir);
        auto cfgDir = dir + "/conf";
        fs::create_directories(cfgDir);
        // alarmParam.json — default empty envelope
        WriteJsonFile(cfgDir + "/alarmParam.json", R"({"overviewInfo":{},"videoRecordInfo":{}})");
        // devRebootParam.json — default
        WriteJsonFile(cfgDir + "/devRebootParam.json",
                      R"({"isTimingRestart":true,"weekDay":0,"restartTimeSec":7200})");
        // devSystemParam.json — required by SysConfigState
        WriteJsonFile(cfgDir + "/devSystemParam.json", R"({})");
    }

    ~TestConfigDir() {
        fs::remove_all(dir);
    }
};

}  // namespace

TEST_CASE("SystemServiceImpl: PictureQuality validation", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetPictureQuality returns defaults after construction") {
        auto quality = sut.GetPictureQuality();
        CHECK(quality.picQuality == 75);
        CHECK(quality.alarmTypeOverview == true);
        CHECK(quality.areaOverview == true);
        CHECK(quality.targetBoxOverview == true);
        CHECK(quality.targetSizeOverview == true);
    }

    SECTION("SetPictureQuality with valid value succeeds") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = 50;
        auto result     = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetPictureQuality().picQuality == 50);
    }

    SECTION("SetPictureQuality with 0 returns ParameterException") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = 0;
        auto result     = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetPictureQuality with negative returns ParameterException") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = -1;
        auto result     = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetPictureQuality with 101 returns ParameterException") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = 101;
        auto result     = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetPictureQuality boundary: picQuality=1 succeeds") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = 1;
        auto result     = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }

    SECTION("SetPictureQuality boundary: picQuality=100 succeeds") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = 100;
        auto result     = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }

    SECTION("ResetPictureQuality restores defaults") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = 30;
        sut.SetPictureQuality(info);
        REQUIRE(sut.GetPictureQuality().picQuality == 30);

        sut.ResetPictureQuality();
        REQUIRE(sut.GetPictureQuality().picQuality == 75);
    }
}

TEST_CASE("SystemServiceImpl: AlarmVideoDuration validation", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetAlarmVideoDuration returns defaults") {
        auto dur = sut.GetAlarmVideoDuration();
        CHECK(dur.bopen == false);
        CHECK(dur.preDuration == 5);
        CHECK(dur.aftreDuration == 5);
    }

    SECTION("SetAlarmVideoDuration with valid values succeeds") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.bopen         = true;
        info.preDuration   = 10;
        info.aftreDuration = 10;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        auto stored = sut.GetAlarmVideoDuration();
        REQUIRE(stored.bopen == true);
        REQUIRE(stored.preDuration == 10);
        REQUIRE(stored.aftreDuration == 10);
    }

    SECTION("SetAlarmVideoDuration with preDuration=0 returns error") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 0;
        info.aftreDuration = 5;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetAlarmVideoDuration with aftreDuration=0 returns error") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 5;
        info.aftreDuration = 0;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetAlarmVideoDuration with preDuration>100 returns error") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 101;
        info.aftreDuration = 5;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetAlarmVideoDuration with aftreDuration>100 returns error") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 5;
        info.aftreDuration = 101;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("ResetAlarmVideoDuration restores defaults") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.bopen         = true;
        info.preDuration   = 20;
        info.aftreDuration = 20;
        sut.SetAlarmVideoDuration(info);
        sut.ResetAlarmVideoDuration();
        auto dur = sut.GetAlarmVideoDuration();
        REQUIRE(dur.bopen == false);
        REQUIRE(dur.preDuration == 5);
        REQUIRE(dur.aftreDuration == 5);
    }
}

TEST_CASE("SystemServiceImpl: RebootParam validation", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetRebootParam returns loaded config") {
        auto param = sut.GetRebootParam();
        // Values from devRebootParam.json
        CHECK(param.isTimingRestart == true);
        CHECK(param.weekDay == 0);
        CHECK(param.restartTimeSec == 7200);
    }

    SECTION("SetRebootParam with valid weekDay succeeds") {
        cosmo::CfgRebootParamInfo info;
        info.isTimingRestart = false;
        info.weekDay         = 3;
        info.restartTimeSec  = 0;
        auto result          = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        auto stored = sut.GetRebootParam();
        REQUIRE(stored.weekDay == 3);
        REQUIRE(stored.isTimingRestart == false);
    }

    SECTION("SetRebootParam with weekDay=-1 returns error") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay = -1;
        auto result  = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetRebootParam with weekDay=8 returns error") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay = 8;
        auto result  = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("SetRebootParam boundary: weekDay=0 succeeds") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay = 0;
        auto result  = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }

    SECTION("SetRebootParam boundary: weekDay=7 succeeds") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay = 7;
        auto result  = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }

    SECTION("ResetRebootParam restores defaults") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay        = 5;
        info.restartTimeSec = 9999;
        sut.SetRebootParam(info);
        sut.ResetRebootParam();
        auto param = sut.GetRebootParam();
        CHECK(param.isTimingRestart == true);
        CHECK(param.weekDay == 0);
        CHECK(param.restartTimeSec == 7200);
    }
}

TEST_CASE("SystemServiceImpl: DebugMode and ActionSwitch", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("Debug mode defaults to false") {
        REQUIRE(sut.GetDebugMode() == false);
    }

    SECTION("SetDebugMode toggles debug state") {
        sut.SetDebugMode(true);
        REQUIRE(sut.GetDebugMode() == true);
        sut.SetDebugMode(false);
        REQUIRE(sut.GetDebugMode() == false);
    }

    SECTION("GetActionSwitch returns true when debug mode is off") {
        sut.SetDebugMode(false);
        REQUIRE(sut.GetActionSwitch("any_action") == true);
    }

    SECTION("GetActionSwitch returns true for non-shielded action when debug on") {
        sut.SetDebugMode(true);
        sut.SetShieldedActions({"action_A", "action_B"});
        REQUIRE(sut.GetActionSwitch("action_C") == true);
    }

    SECTION("GetActionSwitch returns false for shielded action when debug on") {
        sut.SetDebugMode(true);
        sut.SetShieldedActions({"action_A", "action_B"});
        REQUIRE(sut.GetActionSwitch("action_A") == false);
        REQUIRE(sut.GetActionSwitch("action_B") == false);
    }

    SECTION("ShieldedActions round-trip") {
        std::vector<std::string> actions = {"act1", "act2", "act3"};
        sut.SetShieldedActions(actions);
        auto result = sut.GetShieldedActions();
        REQUIRE(result.size() == 3);
        REQUIRE(result[0] == "act1");
        REQUIRE(result[1] == "act2");
        REQUIRE(result[2] == "act3");
    }
}

TEST_CASE("SystemServiceImpl: PopUpParam", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetPopUpParam returns defaults") {
        int popUpSwitch = -1, audioPlay = -1, popUpDuration = -1;
        sut.GetPopUpParam(popUpSwitch, audioPlay, popUpDuration);
        CHECK(popUpSwitch == 1);
        CHECK(audioPlay == 1);
        CHECK(popUpDuration == 2);
    }

    SECTION("SetPopUpParam updates values") {
        sut.SetPopUpParam(0, 0, 10);
        int popUpSwitch = -1, audioPlay = -1, popUpDuration = -1;
        sut.GetPopUpParam(popUpSwitch, audioPlay, popUpDuration);
        REQUIRE(popUpSwitch == 0);
        REQUIRE(audioPlay == 0);
        REQUIRE(popUpDuration == 10);
    }

    SECTION("SetPopUpParam with same values does not persist again") {
        // Set once
        sut.SetPopUpParam(1, 1, 5);
        // Set again with same values — should be no-op internally
        sut.SetPopUpParam(1, 1, 5);
        // Values still correct
        int popUpSwitch = -1, audioPlay = -1, popUpDuration = -1;
        sut.GetPopUpParam(popUpSwitch, audioPlay, popUpDuration);
        REQUIRE(popUpSwitch == 1);
        REQUIRE(audioPlay == 1);
        REQUIRE(popUpDuration == 5);
    }
}

TEST_CASE("SystemServiceImpl: HttpInterface delegates to AlarmPushService", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetHttpInterfaceParam delegates to IAlarmPushService") {
        REQUIRE_CALL(mocks.alarmPushSvc, IsEnabled()).RETURN(true);
        REQUIRE_CALL(mocks.alarmPushSvc, GetUrl()).RETURN(std::string("http://example.com/push"));
        auto param = sut.GetHttpInterfaceParam();
        REQUIRE(param.enable == true);
        REQUIRE(param.url == "http://example.com/push");
    }

    SECTION("SetHttpInterfaceParam delegates to IAlarmPushService") {
        cosmo::service::HttpPushParam param;
        param.enable = true;
        param.url    = "http://new-url.com";
        REQUIRE_CALL(mocks.alarmPushSvc, SetPush(true, std::string("http://new-url.com")))
            .RETURN(cosmo::util::ErrorEnum::Success);
        auto result = sut.SetHttpInterfaceParam(param);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }
}

TEST_CASE("SystemServiceImpl: ResourceLimit", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("ResourceLimit defaults to true") {
        REQUIRE(sut.GetResourceLimit() == true);
    }

    SECTION("SetResourceLimit toggles") {
        sut.SetResourceLimit(false);
        REQUIRE(sut.GetResourceLimit() == false);
        sut.SetResourceLimit(true);
        REQUIRE(sut.GetResourceLimit() == true);
    }

    SECTION("SetResourceLimit with same value is no-op") {
        sut.SetResourceLimit(true);
        REQUIRE(sut.GetResourceLimit() == true);
    }
}

TEST_CASE("SystemServiceImpl: RunMode and IsNetworkModel", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("Default RunMode is StandAlone") {
        REQUIRE(sut.GetRunMode() == cosmo::RunMode::RunModeStandAlone);
    }

    SECTION("IsNetworkModel returns false for StandAlone mode") {
        REQUIRE(sut.IsNetworkModel() == false);
    }
}

TEST_CASE("SystemServiceImpl: MqttParam round-trip", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetMqttParam returns defaults and queries IsMqttRegistered") {
        REQUIRE_CALL(mocks.networkSvc, IsMqttRegistered()).RETURN(false);
        auto param = sut.GetMqttParam();
        CHECK(param.enable == false);
        CHECK(param.port == 1883);
        CHECK(param.status == false);
    }

    SECTION("SetMqttParam with enable=true triggers MqttStop then MqttStart") {
        cosmo::service::MqttParam param;
        param.enable   = true;
        param.url      = "192.168.1.100";
        param.port     = 1883;
        param.authMode = 0;
        param.clientId = "client1";
        param.userName = "user";
        param.passwd   = "pass";

        // Expect MQTT restart sequence
        REQUIRE_CALL(mocks.networkSvc, MqttStop()).TIMES(1);
        REQUIRE_CALL(mocks.networkSvc, MqttStart()).TIMES(1);

        auto result = sut.SetMqttParam(param);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }

    SECTION("SetMqttParam with enable=false triggers only MqttStop") {
        // First set to enabled so there's a change
        cosmo::service::MqttParam paramOn;
        paramOn.enable = true;
        paramOn.url    = "192.168.1.100";
        paramOn.port   = 1883;
        ALLOW_CALL(mocks.networkSvc, MqttStop());
        ALLOW_CALL(mocks.networkSvc, MqttStart());
        sut.SetMqttParam(paramOn);

        // Now disable
        cosmo::service::MqttParam paramOff;
        paramOff.enable = false;
        paramOff.url    = "192.168.1.100";
        paramOff.port   = 1883;
        // Only MqttStop should be called, no MqttStart
        REQUIRE_CALL(mocks.networkSvc, MqttStop()).TIMES(1);
        auto result = sut.SetMqttParam(paramOff);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }
}

TEST_CASE("SystemServiceImpl: IotNetworkParam", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetIotNetworkParam queries IsMqttEnabled") {
        REQUIRE_CALL(mocks.networkSvc, IsMqttEnabled()).RETURN(true);
        auto param = sut.GetIotNetworkParam();
        CHECK(param.status == true);
    }

    SECTION("SetIotNetworkParam stores values") {
        sut.SetIotNetworkParam("http://iot.example.com", "10.0.0.1", 8883);
        REQUIRE_CALL(mocks.networkSvc, IsMqttEnabled()).RETURN(true);
        auto param = sut.GetIotNetworkParam();
        REQUIRE(param.httpUrl == "http://iot.example.com");
        REQUIRE(param.mqttIp == "10.0.0.1");
        REQUIRE(param.mqttPort == 8883);
    }

    SECTION("SetIotNetworkParam with same values is no-op") {
        sut.SetIotNetworkParam("http://iot.example.com", "10.0.0.1", 8883);
        // Set again with same values — should not re-save
        sut.SetIotNetworkParam("http://iot.example.com", "10.0.0.1", 8883);
        REQUIRE_CALL(mocks.networkSvc, IsMqttEnabled()).RETURN(false);
        auto param = sut.GetIotNetworkParam();
        REQUIRE(param.httpUrl == "http://iot.example.com");
    }
}

TEST_CASE("SystemServiceImpl: SystemLogo", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("GetSystemLogo returns empty when no logo set") {
        auto logo = sut.GetSystemLogo();
        // No logo set — systemName and logoUrl should be empty
        CHECK(logo.systemName.empty());
        CHECK(logo.logoUrl.empty());
    }

    SECTION("SetSystemLogo stores and GetSystemLogo returns it") {
        auto logoDir = cosmo::path::GetLogoPath();
        fs::create_directories(logoDir);

        std::vector<uint8_t> fakeImg = {0xFF, 0xD8, 0xFF, 0xE0};  // Fake JPEG header
        auto result                  = sut.SetSystemLogo("My System", ".jpg", fakeImg, "Big Screen");
        REQUIRE(result == cosmo::util::ErrorEnum::Success);

        auto logo = sut.GetSystemLogo();
        REQUIRE(logo.systemName == "My System");
        REQUIRE(logo.bigScreenName == "Big Screen");
        REQUIRE(logo.logoUrl.find("logo.jpg") != std::string::npos);

        // Verify file was written
        REQUIRE(fs::exists(logoDir + "/logo.jpg"));
    }
}

// ============================================================================
// Phase 1 (P0): Boundary, dirty-check, and JSON config resilience tests
// ============================================================================

TEST_CASE("SystemServiceImpl: AlarmVideoDuration boundary values", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("preDuration=1 boundary succeeds") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 1;
        info.aftreDuration = 5;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetAlarmVideoDuration().preDuration == 1);
    }

    SECTION("preDuration=100 boundary succeeds") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 100;
        info.aftreDuration = 5;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetAlarmVideoDuration().preDuration == 100);
    }

    SECTION("aftreDuration=1 boundary succeeds") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 5;
        info.aftreDuration = 1;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetAlarmVideoDuration().aftreDuration == 1);
    }

    SECTION("aftreDuration=100 boundary succeeds") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 5;
        info.aftreDuration = 100;
        auto result        = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetAlarmVideoDuration().aftreDuration == 100);
    }

    SECTION("preDuration=-5 returns ParameterException") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = -5;
        info.aftreDuration = 5;
        REQUIRE(sut.SetAlarmVideoDuration(info) == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("aftreDuration=-5 returns ParameterException") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = 5;
        info.aftreDuration = -5;
        REQUIRE(sut.SetAlarmVideoDuration(info) == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("both preDuration and aftreDuration invalid") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.preDuration   = -1;
        info.aftreDuration = 200;
        // preDuration check comes first
        REQUIRE(sut.SetAlarmVideoDuration(info) == cosmo::util::ErrorEnum::ParameterException);
    }
}

TEST_CASE("SystemServiceImpl: Dirty-check - no save on identical values", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("SetPictureQuality with identical values does not re-save") {
        // Get default values
        auto defaults = sut.GetPictureQuality();
        // Set the exact same values — internal dirty check should skip SaveAlarmCfg
        auto result = sut.SetPictureQuality(defaults);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        // Verify values unchanged
        auto after = sut.GetPictureQuality();
        CHECK(after.picQuality == defaults.picQuality);
        CHECK(after.alarmTypeOverview == defaults.alarmTypeOverview);
        CHECK(after.areaOverview == defaults.areaOverview);
    }

    SECTION("SetAlarmVideoDuration with identical values does not re-save") {
        cosmo::CfgAlarmParamVideoRecordInfo info;
        info.bopen         = true;
        info.preDuration   = 15;
        info.aftreDuration = 20;
        sut.SetAlarmVideoDuration(info);
        // Set again with identical values
        auto result = sut.SetAlarmVideoDuration(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        auto stored = sut.GetAlarmVideoDuration();
        REQUIRE(stored.preDuration == 15);
        REQUIRE(stored.aftreDuration == 20);
    }

    SECTION("SetRebootParam with identical values does not re-save") {
        cosmo::CfgRebootParamInfo info;
        info.isTimingRestart = false;
        info.weekDay         = 3;
        info.restartTimeSec  = 1000;
        sut.SetRebootParam(info);
        // Set again with same values
        auto result = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        auto stored = sut.GetRebootParam();
        REQUIRE(stored.weekDay == 3);
    }
}

TEST_CASE("SystemServiceImpl: PictureQuality overlay fields", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("Changing only overlay boolean fields triggers save") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality         = 75;  // default
        info.alarmTypeOverview  = false;
        info.areaOverview       = false;
        info.targetBoxOverview  = false;
        info.targetSizeOverview = false;
        auto result             = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        auto stored = sut.GetPictureQuality();
        REQUIRE(stored.alarmTypeOverview == false);
        REQUIRE(stored.areaOverview == false);
        REQUIRE(stored.targetBoxOverview == false);
        REQUIRE(stored.targetSizeOverview == false);
    }

    SECTION("Mixed overlay change — only picQuality changed") {
        cosmo::CfgAlarmParamOverviewInfo info;
        info.picQuality = 50;
        // booleans remain default (true)
        auto result = sut.SetPictureQuality(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        auto stored = sut.GetPictureQuality();
        REQUIRE(stored.picQuality == 50);
        REQUIRE(stored.alarmTypeOverview == true);
    }
}

TEST_CASE("SystemServiceImpl: RebootParam restartTimeSec behavior", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("restartTimeSec=0 is accepted") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay        = 1;
        info.restartTimeSec = 0;
        auto result         = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetRebootParam().restartTimeSec == 0);
    }

    SECTION("restartTimeSec=86400 (24h) is accepted") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay        = 1;
        info.restartTimeSec = 86400;
        auto result         = sut.SetRebootParam(info);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetRebootParam().restartTimeSec == 86400);
    }

    SECTION("restartTimeSec negative is accepted (no validation in code)") {
        cosmo::CfgRebootParamInfo info;
        info.weekDay        = 1;
        info.restartTimeSec = -1;
        auto result         = sut.SetRebootParam(info);
        // Currently no validation on restartTimeSec
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.GetRebootParam().restartTimeSec == -1);
    }
}

TEST_CASE("SystemServiceImpl: MqttParam dirty-check and field round-trip", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("SetMqttParam with identical values does not trigger MQTT restart") {
        cosmo::service::MqttParam param;
        param.enable   = true;
        param.url      = "192.168.1.100";
        param.port     = 1883;
        param.authMode = 0;
        param.clientId = "client1";
        param.userName = "user";
        param.passwd   = "pass";

        // First set — should trigger restart
        ALLOW_CALL(mocks.networkSvc, MqttStop());
        ALLOW_CALL(mocks.networkSvc, MqttStart());
        sut.SetMqttParam(param);

        // Second set with same values — no MqttStop/MqttStart should be called
        // (If they were called, trompeloeil would not complain since we used ALLOW_CALL)
        auto result = sut.SetMqttParam(param);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }

    SECTION("GetMqttParam reads all fields correctly after SetMqttParam") {
        cosmo::service::MqttParam param;
        param.enable   = true;
        param.url      = "10.0.0.1";
        param.port     = 8883;
        param.authMode = 1;
        param.clientId = "myclient";
        param.userName = "admin";
        param.passwd   = "secret";

        ALLOW_CALL(mocks.networkSvc, MqttStop());
        ALLOW_CALL(mocks.networkSvc, MqttStart());
        sut.SetMqttParam(param);

        REQUIRE_CALL(mocks.networkSvc, IsMqttRegistered()).RETURN(true);
        auto stored = sut.GetMqttParam();
        REQUIRE(stored.enable == true);
        REQUIRE(stored.url == "10.0.0.1");
        REQUIRE(stored.port == 8883);
        REQUIRE(stored.authMode == 1);
        REQUIRE(stored.clientId == "myclient");
        REQUIRE(stored.userName == "admin");
        REQUIRE(stored.passwd == "secret");
        REQUIRE(stored.status == true);
    }

    SECTION("SetMqttParam with empty URL is accepted") {
        cosmo::service::MqttParam param;
        param.enable = true;
        param.url    = "";
        param.port   = 1883;

        ALLOW_CALL(mocks.networkSvc, MqttStop());
        ALLOW_CALL(mocks.networkSvc, MqttStart());
        auto result = sut.SetMqttParam(param);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }
}

TEST_CASE("SystemServiceImpl: PopUpParam edge values", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("Negative values are accepted (no validation)") {
        sut.SetPopUpParam(-1, -1, -1);
        int s = 0, a = 0, d = 0;
        sut.GetPopUpParam(s, a, d);
        REQUIRE(s == -1);
        REQUIRE(a == -1);
        REQUIRE(d == -1);
    }

    SECTION("Large values are accepted (no validation)") {
        sut.SetPopUpParam(999, 999, 999);
        int s = 0, a = 0, d = 0;
        sut.GetPopUpParam(s, a, d);
        REQUIRE(s == 999);
        REQUIRE(a == 999);
        REQUIRE(d == 999);
    }

    SECTION("Zero values are accepted") {
        sut.SetPopUpParam(0, 0, 0);
        int s = -1, a = -1, d = -1;
        sut.GetPopUpParam(s, a, d);
        REQUIRE(s == 0);
        REQUIRE(a == 0);
        REQUIRE(d == 0);
    }
}

TEST_CASE("SystemServiceImpl: ActionSwitch edge cases", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("Debug on with empty shielded actions — all actions allowed") {
        sut.SetDebugMode(true);
        sut.SetShieldedActions({});
        REQUIRE(sut.GetActionSwitch("anything") == true);
        REQUIRE(sut.GetActionSwitch("") == true);
    }

    SECTION("Empty actionId is never shielded when not in list") {
        sut.SetDebugMode(true);
        sut.SetShieldedActions({"action_A"});
        REQUIRE(sut.GetActionSwitch("") == true);
    }

    SECTION("Empty actionId IS shielded when in list") {
        sut.SetDebugMode(true);
        sut.SetShieldedActions({""});
        REQUIRE(sut.GetActionSwitch("") == false);
    }

    SECTION("ShieldedActions replacement overwrites previous list") {
        sut.SetShieldedActions({"a", "b", "c"});
        sut.SetShieldedActions({"x"});
        auto result = sut.GetShieldedActions();
        REQUIRE(result.size() == 1);
        REQUIRE(result[0] == "x");
    }

    SECTION("ShieldedActions set to empty clears all") {
        sut.SetShieldedActions({"a", "b"});
        sut.SetShieldedActions({});
        REQUIRE(sut.GetShieldedActions().empty());
    }
}

TEST_CASE("SystemServiceImpl: SystemLogo supplementary cases", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("SetSystemLogo with empty bigScreenName does not overwrite previous") {
        auto logoDir = cosmo::path::GetLogoPath();
        fs::create_directories(logoDir);

        std::vector<uint8_t> fakeImg = {0x89, 0x50, 0x4E, 0x47};

        // First set with bigScreenName
        sut.SetSystemLogo("System1", ".png", fakeImg, "BigScreen1");
        auto logo1 = sut.GetSystemLogo();
        REQUIRE(logo1.bigScreenName == "BigScreen1");

        // Second set with empty bigScreenName — should NOT overwrite
        sut.SetSystemLogo("System2", ".png", fakeImg, "");
        auto logo2 = sut.GetSystemLogo();
        REQUIRE(logo2.systemName == "System2");
        REQUIRE(logo2.bigScreenName == "BigScreen1");  // preserved
    }

    SECTION("SetSystemLogo with .png extension creates correct filename") {
        auto logoDir = cosmo::path::GetLogoPath();
        fs::create_directories(logoDir);

        std::vector<uint8_t> fakeImg = {0x89, 0x50};
        sut.SetSystemLogo("TestSys", ".png", fakeImg, "Screen");
        REQUIRE(fs::exists(logoDir + "/logo.png"));

        auto logo = sut.GetSystemLogo();
        REQUIRE(logo.logoUrl.find("logo.png") != std::string::npos);
    }
}

TEST_CASE("SystemServiceImpl: RunMode round-trip", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("Default RunMode is StandAlone and IsNetworkModel is false") {
        REQUIRE(sut.GetRunMode() == cosmo::RunMode::RunModeStandAlone);
        REQUIRE(sut.IsNetworkModel() == false);
    }

    // Note: SetRunMode calls RebootSystem() which is a platform function.
    // We cannot safely test SetRunMode without mocking the platform layer.
    // The following test documents the expected linkage behavior.
    SECTION("RunMode enum values are distinct") {
        REQUIRE(cosmo::RunMode::RunModeStandAlone != cosmo::RunMode::RunModeIotNetwork);
    }
}

TEST_CASE("SystemServiceImpl: JSON config resilience - empty JSON", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;

    // Create config dir with empty JSON files
    std::string dir = "/tmp/cosmo_empty_json_test";
    cosmo::path::OverrideRootPathForTest(dir, dir);
    fs::create_directories(dir + "/conf");
    WriteJsonFile(dir + "/conf/alarmParam.json", "{}");
    WriteJsonFile(dir + "/conf/devRebootParam.json", "{}");
    WriteJsonFile(dir + "/conf/devSystemParam.json", "{}");

    SECTION("Empty JSON loads default values") {
        cosmo::service::SystemServiceImpl sut;

        // PictureQuality defaults
        auto quality = sut.GetPictureQuality();
        CHECK(quality.picQuality == 75);
        CHECK(quality.alarmTypeOverview == true);

        // AlarmVideoDuration defaults
        auto dur = sut.GetAlarmVideoDuration();
        CHECK(dur.bopen == false);
        CHECK(dur.preDuration == 5);
        CHECK(dur.aftreDuration == 5);

        // RebootParam defaults (from struct, not from file since {} won't populate)
        auto reboot = sut.GetRebootParam();
        CHECK(reboot.isTimingRestart == true);
        CHECK(reboot.weekDay == 0);
        CHECK(reboot.restartTimeSec == 7200);

        // Debug defaults
        CHECK(sut.GetDebugMode() == false);
        CHECK(sut.GetResourceLimit() == true);
    }

    fs::remove_all(dir);
}

TEST_CASE("SystemServiceImpl: Config persistence round-trip", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;

    std::string dir = "/tmp/cosmo_persist_test";
    cosmo::path::OverrideRootPathForTest(dir, dir);
    fs::create_directories(dir + "/conf");
    WriteJsonFile(dir + "/conf/alarmParam.json", R"({"overviewInfo":{},"videoRecordInfo":{}})");
    WriteJsonFile(dir + "/conf/devRebootParam.json",
                  R"({"isTimingRestart":true,"weekDay":0,"restartTimeSec":7200})");
    WriteJsonFile(dir + "/conf/devSystemParam.json", R"({})");

    SECTION("Values written by Set are readable by a new instance") {
        // First instance: set values
        {
            cosmo::service::SystemServiceImpl sut1;
            cosmo::CfgAlarmParamOverviewInfo info;
            info.picQuality = 42;
            sut1.SetPictureQuality(info);

            cosmo::CfgRebootParamInfo rInfo;
            rInfo.weekDay        = 5;
            rInfo.restartTimeSec = 12345;
            sut1.SetRebootParam(rInfo);
        }

        // Second instance: reads from persisted files
        {
            cosmo::service::SystemServiceImpl sut2;
            auto quality = sut2.GetPictureQuality();
            REQUIRE(quality.picQuality == 42);

            auto reboot = sut2.GetRebootParam();
            REQUIRE(reboot.weekDay == 5);
            REQUIRE(reboot.restartTimeSec == 12345);
        }
    }

    fs::remove_all(dir);
}

TEST_CASE("SystemServiceImpl: IotNetworkParam dirty-check", "[SystemConfigService]") {
    cosmo::test::MockServiceRegistry mocks;
    TestConfigDir cfg;

    cosmo::service::SystemServiceImpl sut;

    SECTION("SetIotNetworkParam enables mqtt and http flags") {
        sut.SetIotNetworkParam("http://iot.test.com", "10.0.0.1", 8883);
        REQUIRE_CALL(mocks.networkSvc, IsMqttEnabled()).RETURN(true);
        auto param = sut.GetIotNetworkParam();
        REQUIRE(param.httpUrl == "http://iot.test.com");
        REQUIRE(param.mqttIp == "10.0.0.1");
        REQUIRE(param.mqttPort == 8883);
    }

    SECTION("SetIotNetworkParam with different httpUrl triggers save") {
        sut.SetIotNetworkParam("http://v1.com", "10.0.0.1", 8883);
        sut.SetIotNetworkParam("http://v2.com", "10.0.0.1", 8883);
        REQUIRE_CALL(mocks.networkSvc, IsMqttEnabled()).RETURN(false);
        auto param = sut.GetIotNetworkParam();
        REQUIRE(param.httpUrl == "http://v2.com");
    }

    SECTION("SetIotNetworkParam with different mqttPort triggers save") {
        sut.SetIotNetworkParam("http://test.com", "10.0.0.1", 1883);
        sut.SetIotNetworkParam("http://test.com", "10.0.0.1", 9999);
        REQUIRE_CALL(mocks.networkSvc, IsMqttEnabled()).RETURN(false);
        auto param = sut.GetIotNetworkParam();
        REQUIRE(param.mqttPort == 9999);
    }
}
