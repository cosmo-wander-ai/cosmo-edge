#include "catch_amalgamated.hpp"
/*
 * test_camera_task_mng.cc - CameraServiceImpl per-camera task logic tests
 * (Formerly tested CameraTaskMng directly; now tests via CameraServiceImpl
 *  after CameraTaskMng was inlined.)
 */
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "mock/MockAlgorithmService.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockScheduleService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockTaskService.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "service/camera/impl/CameraTaskUnit.h"
#include "util/JsonStructUtil.h"
#include "util/dto/ChannelStatusDto.h"

using namespace cosmo;
using namespace cosmo::service;
using trompeloeil::_;

namespace {
MsgDynamicKeyValue MakeParam(const std::string& key, const std::string& value) {
    MsgDynamicKeyValue param;
    param.key   = key;
    param.value = value;
    return param;
}

std::string FindParamValue(const MsgTaskConfig& config, const std::string& key) {
    auto it = std::find_if(config.params.begin(), config.params.end(),
                           [&](const auto& param) { return param.key == key; });
    return it == config.params.end() ? std::string{} : it->value.ToString();
}

std::string FindParamValue(const std::vector<MsgDynamicKeyValue>& params, const std::string& key) {
    auto it = std::find_if(params.begin(), params.end(), [&](const auto& param) { return param.key == key; });
    return it == params.end() ? std::string{} : it->value.ToString();
}

MsgDynamicElement MakeMetadataParam(const std::string& key, const std::string& value,
                                    std::optional<bool> channel_editable,
                                    std::optional<int> senior = std::nullopt,
                                    const std::string& type   = "text") {
    MsgDynamicElement param;
    param.key             = key;
    param.value           = value;
    param.defaultValue    = value;
    param.type            = type;
    param.channelEditable = channel_editable;
    param.senior          = senior;
    return param;
}

std::string MakeMetadataJson(std::vector<MsgDynamicElement> params) {
    MsgAlgorithmMetaData metadata;
    metadata.params = std::move(params);
    std::string encoded;
    if (!util::EncodeJson(metadata, encoded)) {
        return {};
    }
    return encoded;
}

std::string DefaultTestAlgorithmMetadata() {
    return MakeMetadataJson({MakeMetadataParam("param.threshold", "5", true, 0)});
}

bool SeedCameraTaskParams(const std::string& camera_id, const std::string& algorithm_code,
                          std::vector<MsgDynamicKeyValue> params) {
    const auto task_config_dir =
        std::filesystem::path("/tmp/cosmo_test/conf/camera") / camera_id / algorithm_code;
    std::error_code error;
    std::filesystem::create_directories(task_config_dir, error);
    if (error) {
        return false;
    }
    CameraTaskUnitParam persisted;
    persisted.params = std::move(params);
    return util::SaveStructToJsonFile((task_config_dir / "param.json").string(), persisted);
}

void DrainSwitchThreads(CameraServiceImpl& service) {
    service.NotifyAlgorithmsChanged({"__test_drain_only__"}, false);
}

// Helper: create a CameraServiceImpl + add a single camera for testing
struct TestFixture {
    cosmo::test::MockServiceRegistry mocks;
    CameraServiceImpl svc;
    std::string cameraId;
    std::string algorithmMetadata;

    TestFixture(const std::string& id, const std::string& url,
                std::string metadata = DefaultTestAlgorithmMetadata())
        : cameraId(id), algorithmMetadata(std::move(metadata)) {
        mocks.expectations.push_back(
            NAMED_ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(algorithmMetadata));
        ALLOW_CALL(mocks.taskSvc, TaskCreate(_, _, _, _)).RETURN(util::ErrorEnum::Success);
        ALLOW_CALL(mocks.taskSvc, TaskIsStart(_)).RETURN(false);
        ALLOW_CALL(mocks.taskSvc, TaskStart(_, _)).RETURN(true);
        ALLOW_CALL(mocks.taskSvc, TaskStop(_)).RETURN(true);
        ALLOW_CALL(mocks.taskSvc, TaskDelete(_)).RETURN(util::ErrorEnum::Success);
        ALLOW_CALL(mocks.taskSvc, TaskChannelSetUrl(_, _));

        MsgCameraInfo config;
        config.videoChannelId = id;
        config.channelName    = "test";
        config.url            = url;
        config.channelType    = MsgCameraType::MsgCameraTypeLive;
        std::string outId;
        svc.Add(config, outId);
    }
};
}  // namespace

TEST_CASE("CameraServiceImpl basic task operations", "[CameraServiceImpl]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_01");
    TestFixture fx("test_camera_01", "rtsp://test");

    SECTION("GetTasks initially empty") {
        auto tasks = fx.svc.GetTasks(fx.cameraId);
        REQUIRE(tasks.empty());
    }

    SECTION("ScheduleInUse returns false when no tasks") {
        std::string scheduleId = "sched1";
        REQUIRE_FALSE(fx.svc.ScheduleInUse(scheduleId));
    }

    SECTION("QuerySwitch with non-existent code returns error") {
        bool enable = false;
        auto ret    = fx.svc.QuerySwitch(fx.cameraId, "non_existent", enable);
        REQUIRE(ret == util::ErrorEnum::TaskNotExist);
    }

    SECTION("DeleteTask for non-existent") {
        auto ret = fx.svc.DeleteTask(fx.cameraId, "non_existent");
        REQUIRE(ret == util::ErrorEnum::TaskNotExist);
    }
}

TEST_CASE("CameraServiceImpl reapplies unchanged saved parameters before every restart",
          "[CameraServiceImpl][task-parameters][restart]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_param_restart");

    std::mutex event_mtx;
    std::vector<std::string> events;
    auto record_event = [&](std::string event) {
        std::lock_guard<std::mutex> lock(event_mtx);
        events.push_back(std::move(event));
    };
    auto record_param = [&](const MsgTaskConfig& config) {
        const auto value = FindParamValue(config, "param.threshold");
        if (!value.empty()) {
            record_event("param:" + value);
        }
    };
    const std::string camera_id = "test_camera_param_restart";
    const std::string task_id   = camera_id + "_test_alg";
    TestFixture fx(camera_id, "rtsp://127.0.0.1:1/test");

    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    MsgTaskConfig config;
    MsgDynamicKeyValue threshold;
    threshold.key   = "param.threshold";
    threshold.value = "5";
    config.params.push_back(threshold);

    REQUIRE(fx.svc.SaveOrUpdateTask(fx.cameraId, "test_alg", config, "sched1") == util::ErrorEnum::Success);
    DrainSwitchThreads(fx.svc);

    ALLOW_CALL(fx.mocks.taskSvc, SetTaskParam(fx.cameraId, task_id, _))
        .SIDE_EFFECT(record_param(_3))
        .RETURN(true);
    ALLOW_CALL(fx.mocks.taskSvc, TaskStart(fx.cameraId, task_id))
        .SIDE_EFFECT(record_event("start"))
        .RETURN(true);
    ALLOW_CALL(fx.mocks.taskSvc, TaskStop(task_id)).SIDE_EFFECT(record_event("stop")).RETURN(true);

    for (int restart = 0; restart < 2; ++restart) {
        REQUIRE(fx.svc.SwitchTask(fx.cameraId, "test_alg", false) == util::ErrorEnum::Success);
        DrainSwitchThreads(fx.svc);
        REQUIRE(fx.svc.SwitchTask(fx.cameraId, "test_alg", true) == util::ErrorEnum::Success);
        DrainSwitchThreads(fx.svc);
    }

    const std::vector<std::string> expected_events{"stop", "param:5", "start", "stop", "param:5", "start"};
    std::lock_guard<std::mutex> lock(event_mtx);
    REQUIRE(events == expected_events);
}

TEST_CASE("CameraServiceImpl refuses task start when parameter synchronization fails",
          "[CameraServiceImpl][task-parameters][start-gate]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_param_failure");

    std::atomic<int> param_attempts{0};
    std::atomic<int> start_attempts{0};
    std::mutex value_mtx;
    std::string attempted_value;
    auto record_param_attempt = [&](const MsgTaskConfig& applied) {
        {
            std::lock_guard<std::mutex> lock(value_mtx);
            attempted_value = FindParamValue(applied, "param.threshold");
        }
        param_attempts.fetch_add(1, std::memory_order_relaxed);
    };
    auto record_start_attempt   = [&]() { start_attempts.fetch_add(1, std::memory_order_relaxed); };
    const std::string camera_id = "test_camera_param_failure";
    const std::string task_id   = camera_id + "_test_alg";
    TestFixture fx(camera_id, "rtsp://127.0.0.1:1/test");

    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    MsgTaskConfig config;
    MsgDynamicKeyValue threshold;
    threshold.key   = "param.threshold";
    threshold.value = "5";
    config.params.push_back(threshold);
    REQUIRE(fx.svc.SaveOrUpdateTask(fx.cameraId, "test_alg", config, "sched1") == util::ErrorEnum::Success);
    DrainSwitchThreads(fx.svc);
    REQUIRE(fx.svc.SwitchTask(fx.cameraId, "test_alg", false) == util::ErrorEnum::Success);
    DrainSwitchThreads(fx.svc);

    ALLOW_CALL(fx.mocks.taskSvc, SetTaskParam(fx.cameraId, task_id, _))
        .SIDE_EFFECT(record_param_attempt(_3))
        .RETURN(false);
    ALLOW_CALL(fx.mocks.taskSvc, TaskStart(fx.cameraId, task_id))
        .SIDE_EFFECT(record_start_attempt())
        .RETURN(true);

    REQUIRE(fx.svc.SwitchTask(fx.cameraId, "test_alg", true) == util::ErrorEnum::Success);
    DrainSwitchThreads(fx.svc);

    REQUIRE(param_attempts.load(std::memory_order_relaxed) == 1);
    REQUIRE(start_attempts.load(std::memory_order_relaxed) == 0);
    std::lock_guard<std::mutex> lock(value_mtx);
    REQUIRE(attempted_value == "5");
}

TEST_CASE("CameraServiceImpl preserves parameter ownership across saves",
          "[CameraServiceImpl][task-parameters][ownership]") {
    const std::string camera_id = "test_camera_param_ownership";
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_param_ownership");

    const auto metadata = MakeMetadataJson({MakeMetadataParam("param.sceneOwned", "10", false, 2),
                                            MakeMetadataParam("param.visible", "1", true, 0)});

    TestFixture fx(camera_id, "rtsp://127.0.0.1:1/test", metadata);
    REQUIRE(SeedCameraTaskParams(camera_id, "test_alg",
                                 {MakeParam("param.sceneOwned", "5"), MakeParam("param.visible", "2")}));
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    // The task is not running yet. A stale hidden value in both param.json and the save request must not
    // replace the scene baseline; the visible value is a valid channel override.
    MsgTaskConfig initial_save;
    initial_save.params = {MakeParam("param.sceneOwned", "7"), MakeParam("param.visible", "3")};
    REQUIRE(fx.svc.SaveOrUpdateTask(camera_id, "test_alg", initial_save, "sched1") ==
            util::ErrorEnum::Success);
    DrainSwitchThreads(fx.svc);

    std::vector<MsgDynamicKeyValue> saved;
    REQUIRE(fx.svc.QueryTaskParam(camera_id, "test_alg", saved) == util::ErrorEnum::Success);
    CHECK(FindParamValue(saved, "param.sceneOwned") == "10");
    CHECK(FindParamValue(saved, "param.visible") == "3");

    // Saving while enabled must obey the same ownership contract.
    MsgTaskConfig running_save;
    running_save.params = {MakeParam("param.sceneOwned", "8"), MakeParam("param.visible", "4")};
    REQUIRE(fx.svc.SaveOrUpdateTask(camera_id, "test_alg", running_save, "sched1") ==
            util::ErrorEnum::Success);
    REQUIRE(fx.svc.QueryTaskParam(camera_id, "test_alg", saved) == util::ErrorEnum::Success);
    CHECK(FindParamValue(saved, "param.sceneOwned") == "10");
    CHECK(FindParamValue(saved, "param.visible") == "4");
}

TEST_CASE("CameraServiceImpl rebuilds assigned tasks with the latest scene-managed values",
          "[CameraServiceImpl][task-parameters][ownership][scene-reload]") {
    const std::string camera_id = "test_camera_scene_param_reload";
    const std::string task_id   = camera_id + "_test_alg";
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_scene_param_reload");

    const auto initial_metadata = MakeMetadataJson({MakeMetadataParam("param.sceneOwned", "10", false, 2),
                                                    MakeMetadataParam("param.visible", "1", true, 0)});
    TestFixture fx(camera_id, "rtsp://127.0.0.1:1/test", initial_metadata);
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    MsgTaskConfig initial_save;
    initial_save.params = {MakeParam("param.sceneOwned", "7"), MakeParam("param.visible", "8")};
    REQUIRE(fx.svc.SaveOrUpdateTask(camera_id, "test_alg", initial_save, "sched1") ==
            util::ErrorEnum::Success);
    DrainSwitchThreads(fx.svc);

    const auto updated_metadata = MakeMetadataJson({MakeMetadataParam("param.sceneOwned", "12", false, 2),
                                                    MakeMetadataParam("param.visible", "1", true, 0)});
    REQUIRE_CALL(fx.mocks.algSvc, GetMetaData("test_alg")).RETURN(updated_metadata);
    REQUIRE_CALL(fx.mocks.taskSvc, TaskIsStart(task_id)).RETURN(true);

    std::vector<MsgTaskConfig> applied;
    ALLOW_CALL(fx.mocks.taskSvc, SetTaskParam(camera_id, task_id, _))
        .LR_SIDE_EFFECT(applied.push_back(_3))
        .RETURN(true);
    size_t starts = 0;
    ALLOW_CALL(fx.mocks.taskSvc, TaskStart(camera_id, task_id)).LR_SIDE_EFFECT(starts += 1).RETURN(true);

    fx.svc.NotifyAlgorithmsChanged({"test_alg"}, true);

    std::vector<MsgDynamicKeyValue> saved;
    REQUIRE(fx.svc.QueryTaskParam(camera_id, "test_alg", saved) == util::ErrorEnum::Success);
    CHECK(FindParamValue(saved, "param.sceneOwned") == "12");
    CHECK(FindParamValue(saved, "param.visible") == "8");
    REQUIRE_FALSE(applied.empty());
    CHECK(FindParamValue(applied.back(), "param.sceneOwned") == "12");
    CHECK(FindParamValue(applied.back(), "param.visible") == "8");
    CHECK(starts == 1);
}

TEST_CASE("CameraServiceImpl SaveOrUpdateTask commits configuration atomically",
          "[CameraServiceImpl][save-or-update]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_atomic");
    TestFixture fx("test_camera_atomic", "rtsp://test");

    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("missing", _)).RETURN(false);

    MsgTaskConfig params;
    MsgDynamicKeyValue threshold;
    threshold.key   = "param.threshold";
    threshold.value = "10";
    params.params.push_back(threshold);
    MsgTaskArea area;
    area.name = "zone-1";
    params.areas.push_back(area);

    SECTION("successful save creates one enabled task with all configuration") {
        auto ret = fx.svc.SaveOrUpdateTask(fx.cameraId, "test_alg", params, "sched1");
        REQUIRE(ret == util::ErrorEnum::Success);

        auto tasks = fx.svc.GetTasks(fx.cameraId);
        REQUIRE(tasks.size() == 1);
        CHECK(tasks[0].algorithmCode == "test_alg");
        CHECK(tasks[0].scheduleId == "sched1");
        CHECK(tasks[0].enable);

        std::vector<MsgDynamicKeyValue> saved_params;
        REQUIRE(fx.svc.QueryTaskParam(fx.cameraId, "test_alg", saved_params) == util::ErrorEnum::Success);
        REQUIRE(saved_params.size() == 1);
        CHECK(saved_params[0].value == "10");

        std::vector<MsgTaskArea> saved_areas;
        std::vector<MsgTaskArea> saved_shielded_areas;
        REQUIRE(fx.svc.QueryTaskArea(fx.cameraId, "test_alg", saved_areas, saved_shielded_areas) ==
                util::ErrorEnum::Success);
        REQUIRE(saved_areas.size() == 1);
        CHECK(saved_areas[0].name == "zone-1");
    }

    SECTION("validation failure leaves a new task absent") {
        auto ret = fx.svc.SaveOrUpdateTask(fx.cameraId, "test_alg", params, "missing");
        REQUIRE(ret == util::ErrorEnum::TimeTemplateNotExist);
        CHECK(fx.svc.GetTasks(fx.cameraId).empty());

        std::vector<MsgDynamicKeyValue> saved_params;
        CHECK(fx.svc.QueryTaskParam(fx.cameraId, "test_alg", saved_params) == util::ErrorEnum::TaskNotExist);
    }

    SECTION("validation failure preserves an existing task") {
        REQUIRE(fx.svc.SaveOrUpdateTask(fx.cameraId, "test_alg", params, "sched1") ==
                util::ErrorEnum::Success);

        params.params[0].value = "20";
        auto ret               = fx.svc.SaveOrUpdateTask(fx.cameraId, "test_alg", params, "missing");
        REQUIRE(ret == util::ErrorEnum::TimeTemplateNotExist);

        std::vector<MsgDynamicKeyValue> saved_params;
        REQUIRE(fx.svc.QueryTaskParam(fx.cameraId, "test_alg", saved_params) == util::ErrorEnum::Success);
        REQUIRE(saved_params.size() == 1);
        CHECK(saved_params[0].value == "10");
    }
}

TEST_CASE("CameraServiceImpl rejects duplicate parameter keys before creating a task",
          "[CameraServiceImpl][task-parameters][validation]") {
    const std::string camera_id  = "test_camera_duplicate_param_new";
    const auto camera_config_dir = std::filesystem::path("/tmp/cosmo_test/conf/camera") / camera_id;
    std::filesystem::remove_all(camera_config_dir);

    TestFixture fx(camera_id, "rtsp://127.0.0.1:1/test");
    REQUIRE(std::filesystem::exists(camera_config_dir));

    FORBID_CALL(fx.mocks.taskSvc, TaskCreate(_, _, _, _));
    FORBID_CALL(fx.mocks.taskSvc, SetTaskParam(_, _, _));

    MsgTaskConfig duplicate_params;
    duplicate_params.params = {MakeParam("param.threshold", "10"), MakeParam("param.threshold", "20")};

    CHECK(fx.svc.SaveOrUpdateTask(camera_id, "test_alg", duplicate_params, "sched1") ==
          util::ErrorEnum::InvalidParam);
    CHECK(fx.svc.ModifyTaskParam(camera_id, "test_alg", duplicate_params) == util::ErrorEnum::InvalidParam);

    CHECK(fx.svc.GetTasks(camera_id).empty());
    std::vector<MsgDynamicKeyValue> saved_params;
    CHECK(fx.svc.QueryTaskParam(camera_id, "test_alg", saved_params) == util::ErrorEnum::TaskNotExist);
    std::vector<MsgTaskArea> saved_areas;
    std::vector<MsgTaskArea> saved_shielded_areas;
    CHECK(fx.svc.QueryTaskArea(camera_id, "test_alg", saved_areas, saved_shielded_areas) ==
          util::ErrorEnum::TaskNotExist);

    CHECK_FALSE(std::filesystem::exists(camera_config_dir / "test_alg"));
    CHECK_FALSE(std::filesystem::exists(camera_config_dir / "taskList.json"));
}

TEST_CASE("CameraServiceImpl duplicate parameter requests preserve an existing task snapshot",
          "[CameraServiceImpl][task-parameters][validation]") {
    const std::string camera_id  = "test_camera_duplicate_param_existing";
    const auto camera_config_dir = std::filesystem::path("/tmp/cosmo_test/conf/camera") / camera_id;
    std::filesystem::remove_all(camera_config_dir);

    TestFixture fx(camera_id, "rtsp://127.0.0.1:1/test");
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    MsgTaskConfig initial_params;
    initial_params.params = {MakeParam("param.threshold", "10")};
    MsgTaskArea initial_area;
    initial_area.name    = "original-zone";
    initial_params.areas = {initial_area};
    REQUIRE(fx.svc.SaveOrUpdateTask(camera_id, "test_alg", initial_params, "sched1") ==
            util::ErrorEnum::Success);
    DrainSwitchThreads(fx.svc);

    auto check_original_snapshot = [&]() {
        const auto tasks = fx.svc.GetTasks(camera_id);
        REQUIRE(tasks.size() == 1);
        CHECK(tasks[0].algorithmCode == "test_alg");
        CHECK(tasks[0].scheduleId == "sched1");
        CHECK(tasks[0].enable);

        std::vector<MsgDynamicKeyValue> saved_params;
        REQUIRE(fx.svc.QueryTaskParam(camera_id, "test_alg", saved_params) == util::ErrorEnum::Success);
        REQUIRE(saved_params.size() == 1);
        CHECK(saved_params[0].key == "param.threshold");
        CHECK(saved_params[0].value == "10");

        std::vector<MsgTaskArea> saved_areas;
        std::vector<MsgTaskArea> saved_shielded_areas;
        REQUIRE(fx.svc.QueryTaskArea(camera_id, "test_alg", saved_areas, saved_shielded_areas) ==
                util::ErrorEnum::Success);
        REQUIRE(saved_areas.size() == 1);
        CHECK(saved_areas[0].name == "original-zone");
        CHECK(saved_shielded_areas.empty());
    };
    check_original_snapshot();

    FORBID_CALL(fx.mocks.taskSvc, TaskCreate(_, _, _, _));
    FORBID_CALL(fx.mocks.taskSvc, SetTaskParam(_, _, _));

    MsgTaskConfig duplicate_params;
    duplicate_params.params = {MakeParam("param.threshold", "20"), MakeParam("param.threshold", "30")};
    MsgTaskArea rejected_area;
    rejected_area.name     = "must-not-replace-original";
    duplicate_params.areas = {rejected_area};

    CHECK(fx.svc.SaveOrUpdateTask(camera_id, "test_alg", duplicate_params, "sched1") ==
          util::ErrorEnum::InvalidParam);
    check_original_snapshot();

    CHECK(fx.svc.ModifyTaskParam(camera_id, "test_alg", duplicate_params) == util::ErrorEnum::InvalidParam);
    check_original_snapshot();
}

#ifdef COSMO_NN_USE_SOPHON_BACKEND
TEST_CASE("CameraServiceImpl resource rejection leaves no partial task",
          "[CameraServiceImpl][save-or-update][sophon]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_resource");
    TestFixture fx("test_camera_resource", "rtsp://test");

    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);
    REQUIRE_CALL(fx.mocks.configReadSvc, GetResourceLimit()).RETURN(true);
    REQUIRE_CALL(fx.mocks.taskSvc, PacketStatus(_, _, _, _))
        .LR_SIDE_EFFECT(_1 = 100; _2 = 0; _3 = 100; _4 = 30);

    MsgGpuInfo gpu_info;
    gpu_info.gpuusage        = 1.0;
    gpu_info.gpumemtotal     = 8000;
    gpu_info.gpumemavailable = 0;
    MsgGpuDevUsage device;
    device.gpumemtotal     = 8000;
    device.gpumemavailable = 0;
    gpu_info.gpudevusage.push_back(device);
    REQUIRE_CALL(fx.mocks.deviceInfoSvc, GetGpuUtilization()).RETURN(gpu_info);

    MsgTaskConfig params;
    MsgDynamicKeyValue threshold;
    threshold.key   = "param.threshold";
    threshold.value = "20";
    params.params.push_back(threshold);
    MsgTaskArea area;
    area.name = "must-not-persist";
    params.areas.push_back(area);

    auto ret = fx.svc.SaveOrUpdateTask(fx.cameraId, "test_alg", params, "sched1");
    REQUIRE(ret == util::ErrorEnum::ResourceLimit);
    CHECK(fx.svc.GetTasks(fx.cameraId).empty());

    std::vector<MsgDynamicKeyValue> saved_params;
    CHECK(fx.svc.QueryTaskParam(fx.cameraId, "test_alg", saved_params) == util::ErrorEnum::TaskNotExist);
}
#endif

TEST_CASE("CameraServiceImpl monitor logic", "[CameraServiceImpl]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_01");
    TestFixture fx("test_camera_01", "rtsp://test");

    ALLOW_CALL(fx.mocks.appInfoSvc, GetNumber()).RETURN(1);
    ALLOW_CALL(fx.mocks.appInfoSvc, GetOverviewStructureRecord()).RETURN(false);
    ALLOW_CALL(fx.mocks.appInfoSvc, GetModelDebug()).RETURN(false);

    // Setup schedule exist mock
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    REQUIRE(fx.svc.ModifyTaskStrategy(fx.cameraId, "test_alg", "sched1") == util::ErrorEnum::Success);

    SECTION("Task starts when enabled, in schedule, and authed") {
        fx.svc.SwitchTask(fx.cameraId, "test_alg", true);  // enable

        ALLOW_CALL(fx.mocks.taskSvc, TaskIsStart("test_camera_01_test_alg")).RETURN(false);
        ALLOW_CALL(fx.mocks.scheduleSvc, InRunTime("sched1")).RETURN(true);

        // Monitor is triggered by the periodic timer; we can't directly call it
        // but verify the task was enabled
        bool enable = false;
        fx.svc.QuerySwitch(fx.cameraId, "test_alg", enable);
        REQUIRE(enable == true);
    }

    SECTION("Task disable works") {
        fx.svc.SwitchTask(fx.cameraId, "test_alg", true);   // enable first
        fx.svc.SwitchTask(fx.cameraId, "test_alg", false);  // disable

        bool enable = true;
        fx.svc.QuerySwitch(fx.cameraId, "test_alg", enable);
        REQUIRE(enable == false);
    }
}

TEST_CASE("CameraServiceImpl concurrent task operations", "[CameraServiceImpl][concurrency]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_02");
    TestFixture fx("test_camera_02", "rtsp://test2");

    std::atomic<bool> stop{false};
    std::atomic<int> successCount{0};

    // Setup initial state
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", trompeloeil::_)).RETURN(true);
    fx.svc.ModifyTaskStrategy(fx.cameraId, "test_alg", "sched1");

    std::vector<std::thread> threads;

    // Concurrent Switch
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 5 && !stop.load(std::memory_order_relaxed); ++j) {
                auto ret = fx.svc.SwitchTask(fx.cameraId, "test_alg", j % 2 == 0);
                if (ret == cosmo::util::ErrorEnum::Success) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(successCount.load() > 0);

    // Wait for any detached SwitchCameraTaskAsync threads to finish before mocks are destroyed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
