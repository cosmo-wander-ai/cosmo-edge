#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
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

std::string MakeThresholdMetadata() {
    return R"json({"params":[{"key":"param.threshold","value":"5","defaultValue":"5","type":"text","senior":0,"channelEditable":true}]})json";
}
}  // namespace

TEST_CASE("CameraTaskUnit pending apply does not wait for an in-flight service call",
          "[CameraTaskUnit][task-parameters][concurrency][liveness]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_param_single_flight";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeThresholdMetadata());
    CameraTaskUnit unit(config_root.string(), "single_flight_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("6")) == util::ErrorEnum::Success);

    std::mutex gate_mtx;
    std::condition_variable apply_entered_cv;
    std::condition_variable release_apply_cv;
    bool apply_entered = false;
    bool release_apply = false;
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("single_flight_channel", "single_flight_channel_test_alg", _))
        .LR_SIDE_EFFECT({
            std::unique_lock<std::mutex> lock(gate_mtx);
            apply_entered = true;
            apply_entered_cv.notify_one();
            release_apply_cv.wait(lock, [&]() { return release_apply; });
        })
        .RETURN(true);

    bool first_result = false;
    std::thread first_apply([&]() { first_result = unit.ApplyLatestTaskConfig(); });

    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(gate_mtx);
        entered = apply_entered_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return apply_entered; });
    }

    std::promise<bool> second_result_promise;
    auto second_result_future = second_result_promise.get_future();
    std::thread second_apply;
    bool second_completed_while_first_was_in_service = false;
    if (entered) {
        second_apply = std::thread([&]() { second_result_promise.set_value(unit.ApplyLatestTaskConfig()); });
        second_completed_while_first_was_in_service =
            second_result_future.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
    }

    {
        std::lock_guard<std::mutex> lock(gate_mtx);
        release_apply = true;
    }
    release_apply_cv.notify_one();
    first_apply.join();
    if (second_apply.joinable()) {
        second_apply.join();
    }

    REQUIRE(entered);
    CHECK(second_completed_while_first_was_in_service);
    CHECK(first_result);
    CHECK_FALSE(second_result_future.get());
}

TEST_CASE("CameraTaskUnit pending apply leaves a newer generation for the next call",
          "[CameraTaskUnit][task-parameters][concurrency][liveness]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_param_bounded_apply";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeThresholdMetadata());
    CameraTaskUnit unit(config_root.string(), "bounded_apply_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("6")) == util::ErrorEnum::Success);

    trompeloeil::sequence apply_sequence;
    std::vector<std::string> applied_values;
    bool callback_update_succeeded = false;
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("bounded_apply_channel", "bounded_apply_channel_test_alg", _))
        .IN_SEQUENCE(apply_sequence)
        .LR_SIDE_EFFECT({
            applied_values.push_back(FindThresholdValue(_3));
            callback_update_succeeded = unit.SetParams(MakeThresholdConfig("7")) == util::ErrorEnum::Success;
        })
        .RETURN(true);
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("bounded_apply_channel", "bounded_apply_channel_test_alg", _))
        .IN_SEQUENCE(apply_sequence)
        .LR_SIDE_EFFECT({ applied_values.push_back(FindThresholdValue(_3)); })
        .RETURN(true);

    CHECK_FALSE(unit.ApplyLatestTaskConfig());
    CHECK(callback_update_succeeded);
    const std::vector<std::string> first_attempt_values{"6"};
    CHECK(applied_values == first_attempt_values);

    CHECK(unit.ApplyLatestTaskConfig());
    const std::vector<std::string> expected_values{"6", "7"};
    CHECK(applied_values == expected_values);
}

TEST_CASE("CameraTaskUnit before-start apply waits for the current applier and then completes",
          "[CameraTaskUnit][task-parameters][concurrency][deadlock]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_param_before_start";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeThresholdMetadata());
    CameraTaskUnit unit(config_root.string(), "before_start_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("6")) == util::ErrorEnum::Success);

    std::mutex gate_mtx;
    std::condition_variable first_entered_cv;
    std::condition_variable release_first_cv;
    bool first_entered = false;
    bool release_first = false;
    size_t apply_count = 0;
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("before_start_channel", "before_start_channel_test_alg", _))
        .TIMES(2)
        .LR_SIDE_EFFECT({
            std::unique_lock<std::mutex> lock(gate_mtx);
            ++apply_count;
            if (apply_count == 1) {
                first_entered = true;
                first_entered_cv.notify_one();
                release_first_cv.wait(lock, [&]() { return release_first; });
            }
        })
        .RETURN(true);

    bool pending_result = false;
    std::thread pending_apply([&]() { pending_result = unit.ApplyLatestTaskConfig(); });

    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(gate_mtx);
        entered = first_entered_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return first_entered; });
    }

    auto before_start_result = std::async(std::launch::async, [&]() {
        return unit.ApplyLatestTaskConfig(CameraTaskUnit::ParamApplyMode::kBeforeStart);
    });
    const bool waited_for_current_apply =
        before_start_result.wait_for(std::chrono::milliseconds(250)) == std::future_status::timeout;

    {
        std::lock_guard<std::mutex> lock(gate_mtx);
        release_first = true;
    }
    release_first_cv.notify_one();
    pending_apply.join();

    const bool before_start_completed =
        before_start_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready;

    REQUIRE(entered);
    CHECK(waited_for_current_apply);
    REQUIRE(before_start_completed);
    CHECK(pending_result);
    CHECK(before_start_result.get());
    CHECK(apply_count == 2);
}

TEST_CASE("CameraTaskUnit keeps a failed generation pending for retry",
          "[CameraTaskUnit][task-parameters][retry]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_param_retry";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeThresholdMetadata());
    CameraTaskUnit unit(config_root.string(), "retry_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("6")) == util::ErrorEnum::Success);

    trompeloeil::sequence apply_sequence;
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("retry_channel", "retry_channel_test_alg", _))
        .IN_SEQUENCE(apply_sequence)
        .RETURN(false);
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("retry_channel", "retry_channel_test_alg", _))
        .IN_SEQUENCE(apply_sequence)
        .RETURN(true);

    CHECK_FALSE(unit.ApplyLatestTaskConfig());
    CHECK(unit.ApplyLatestTaskConfig());
    FORBID_CALL(mocks.taskSvc, SetTaskParam("retry_channel", "retry_channel_test_alg", _));
    CHECK(unit.ApplyLatestTaskConfig());
}

TEST_CASE("CameraTaskUnit reapplies an unchanged snapshot before each start",
          "[CameraTaskUnit][task-parameters][restart]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_param_restart";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeThresholdMetadata());
    CameraTaskUnit unit(config_root.string(), "restart_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("5")) == util::ErrorEnum::Success);

    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("restart_channel", "restart_channel_test_alg", _))
        .TIMES(3)
        .RETURN(true);

    CHECK(unit.ApplyLatestTaskConfig());
    CHECK(unit.ApplyLatestTaskConfig(CameraTaskUnit::ParamApplyMode::kBeforeStart));
    CHECK(unit.ApplyLatestTaskConfig(CameraTaskUnit::ParamApplyMode::kBeforeStart));
}

TEST_CASE("CameraTaskUnit rejects same-thread before-start reentry without deadlock",
          "[CameraTaskUnit][task-parameters][concurrency][deadlock][reentry]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_param_reentry";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeThresholdMetadata());
    CameraTaskUnit unit(config_root.string(), "reentry_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("8")) == util::ErrorEnum::Success);

    bool reentrant_result = true;
    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("reentry_channel", "reentry_channel_test_alg", _))
        .LR_SIDE_EFFECT(reentrant_result =
                            unit.ApplyLatestTaskConfig(CameraTaskUnit::ParamApplyMode::kBeforeStart))
        .RETURN(true);

    CHECK(unit.ApplyLatestTaskConfig());
    CHECK_FALSE(reentrant_result);
}

TEST_CASE("CameraTaskUnit serializes a burst of concurrent parameter applies",
          "[CameraTaskUnit][task-parameters][concurrency][deadlock][stress]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_param_burst";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeThresholdMetadata());
    CameraTaskUnit unit(config_root.string(), "burst_channel", "test_alg", {});
    REQUIRE(unit.SetParams(MakeThresholdConfig("9")) == util::ErrorEnum::Success);

    REQUIRE_CALL(mocks.taskSvc, SetTaskParam("burst_channel", "burst_channel_test_alg", _))
        .LR_SIDE_EFFECT(std::this_thread::sleep_for(std::chrono::milliseconds(50)))
        .RETURN(true);

    constexpr size_t kThreadCount = 32;
    std::mutex start_mtx;
    std::condition_variable start_cv;
    size_t ready_count = 0;
    bool start         = false;
    std::atomic<size_t> success_count{0};
    std::atomic<size_t> rejected_count{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (size_t index = 0; index < kThreadCount; ++index) {
        threads.emplace_back([&]() {
            {
                std::unique_lock<std::mutex> lock(start_mtx);
                ++ready_count;
                start_cv.notify_all();
                start_cv.wait(lock, [&]() { return start; });
            }
            if (unit.ApplyLatestTaskConfig()) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                rejected_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    {
        std::unique_lock<std::mutex> lock(start_mtx);
        REQUIRE(
            start_cv.wait_for(lock, std::chrono::seconds(5), [&]() { return ready_count == kThreadCount; }));
        start = true;
    }
    start_cv.notify_all();
    for (auto& thread : threads) {
        thread.join();
    }

    CHECK(success_count.load(std::memory_order_relaxed) >= 1);
    CHECK(rejected_count.load(std::memory_order_relaxed) >= 1);
    CHECK(success_count.load(std::memory_order_relaxed) + rejected_count.load(std::memory_order_relaxed) ==
          kThreadCount);
}
