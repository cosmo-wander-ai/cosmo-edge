#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "mock/MockServiceRegistry.h"
#include "mock/MockTaskService.h"
#include "service/camera/impl/CameraTaskUnit.h"

using namespace cosmo;
using trompeloeil::_;

namespace {
MsgTaskConfig MakeThresholdConfig(const std::string& value) {
    MsgTaskConfig config;
    MsgDynamicKeyValue threshold;
    threshold.key   = "param.threshold";
    threshold.value = value;
    config.params.push_back(std::move(threshold));
    return config;
}

std::string FindThresholdValue(const MsgTaskConfig& config) {
    auto it = std::find_if(config.params.begin(), config.params.end(),
                           [](const auto& param) { return param.key == std::string{"param.threshold"}; });
    return it == config.params.end() ? std::string{} : it->value.ToString();
}
}  // namespace

TEST_CASE("CameraTaskUnit applies a newer generation that arrives during synchronization",
          "[CameraTaskUnit][task-parameters][concurrency]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_unit_generation";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    CameraTaskUnit unit(config_root.string(), "generation_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("6")) == util::ErrorEnum::Success);

    std::mutex state_mtx;
    std::condition_variable first_apply_entered_cv;
    std::condition_variable release_first_apply_cv;
    bool first_apply_entered = false;
    bool release_first_apply = false;
    std::vector<std::string> applied_values;

    auto on_set_param = [&](const MsgTaskConfig& config) {
        const auto value = FindThresholdValue(config);
        std::unique_lock<std::mutex> lock(state_mtx);
        applied_values.push_back(value);
        if (value == "6") {
            first_apply_entered = true;
            first_apply_entered_cv.notify_one();
            release_first_apply_cv.wait(lock, [&]() { return release_first_apply; });
        }
    };
    ALLOW_CALL(mocks.taskSvc, SetTaskParam("generation_channel", "generation_channel_test_alg", _))
        .SIDE_EFFECT(on_set_param(_3))
        .RETURN(true);

    bool apply_result = false;
    std::thread apply_thread([&]() { apply_result = unit.TaskEnableParam(); });

    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(state_mtx);
        entered = first_apply_entered_cv.wait_for(lock, std::chrono::seconds(5),
                                                  [&]() { return first_apply_entered; });
    }
    auto concurrent_set_result = util::ErrorEnum::Failed;
    if (entered) {
        concurrent_set_result = unit.SetParams(MakeThresholdConfig("7"));
    }
    {
        std::lock_guard<std::mutex> lock(state_mtx);
        release_first_apply = true;
    }
    release_first_apply_cv.notify_one();
    apply_thread.join();

    REQUIRE(entered);
    REQUIRE(concurrent_set_result == util::ErrorEnum::Success);
    REQUIRE(apply_result);
    const std::vector<std::string> expected_values{"6", "7"};
    REQUIRE(applied_values == expected_values);
    const auto applied_count = applied_values.size();
    REQUIRE(unit.TaskEnableParam());
    REQUIRE(applied_values.size() == applied_count);
}

TEST_CASE("CameraTaskUnit keeps a failed generation pending for retry",
          "[CameraTaskUnit][task-parameters][retry]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_unit_retry";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    CameraTaskUnit unit(config_root.string(), "retry_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("6")) == util::ErrorEnum::Success);

    trompeloeil::sequence apply_sequence;
    std::vector<std::string> applied_values;
    auto record_value = [&](const MsgTaskConfig& config) {
        applied_values.push_back(FindThresholdValue(config));
    };
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("retry_channel", "retry_channel_test_alg", _))
        .IN_SEQUENCE(apply_sequence)
        .SIDE_EFFECT(record_value(_3))
        .RETURN(false);
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("retry_channel", "retry_channel_test_alg", _))
        .IN_SEQUENCE(apply_sequence)
        .SIDE_EFFECT(record_value(_3))
        .RETURN(true);

    REQUIRE_FALSE(unit.TaskEnableParam());
    REQUIRE(unit.TaskEnableParam());
    const std::vector<std::string> expected_values{"6", "6"};
    REQUIRE(applied_values == expected_values);
    // The successful retry advances the applied generation, so this is a no-op.
    FORBID_CALL(mocks.taskSvc, SetTaskParam("retry_channel", "retry_channel_test_alg", _));
    REQUIRE(unit.TaskEnableParam());
}

TEST_CASE("CameraTaskUnit reapplies an unchanged snapshot before each start",
          "[CameraTaskUnit][task-parameters][restart]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_unit_restart";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    CameraTaskUnit unit(config_root.string(), "restart_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("5")) == util::ErrorEnum::Success);

    std::vector<std::string> applied_values;
    ALLOW_CALL(mocks.taskSvc, SetTaskParam("restart_channel", "restart_channel_test_alg", _))
        .SIDE_EFFECT(applied_values.push_back(FindThresholdValue(_3)))
        .RETURN(true);

    REQUIRE(unit.TaskEnableParam());
    const auto after_pending_apply = applied_values.size();
    REQUIRE(unit.TaskEnableParam());
    REQUIRE(applied_values.size() == after_pending_apply);

    REQUIRE(unit.TaskEnableParam(CameraTaskUnit::ParamApplyMode::kBeforeStart));
    REQUIRE(unit.TaskEnableParam(CameraTaskUnit::ParamApplyMode::kBeforeStart));
    REQUIRE(applied_values.size() == after_pending_apply + 2);
    CHECK(applied_values[after_pending_apply] == "5");
    CHECK(applied_values[after_pending_apply + 1] == "5");
}
