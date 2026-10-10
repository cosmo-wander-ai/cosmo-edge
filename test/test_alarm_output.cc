#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

#include "catch_amalgamated.hpp"
#include "linkage/AlarmOutputController.h"
#include "linkage/LinkAgeAlarmOutput.h"
#include "linkage/LinkAgeTask.h"

using namespace std::chrono_literals;
using namespace cosmo::linkage;

namespace {
struct PinState {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<bool> writes;
    bool fail_activation{false};
    int reset_failures{0};

    bool WaitForReset(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, timeout, [&] { return writes.size() >= 2 && !writes.back(); });
    }
    std::vector<bool> Writes() {
        std::lock_guard<std::mutex> lock(mutex);
        return writes;
    }
};

class FakePin : public AlarmOutputPin {
public:
    explicit FakePin(std::shared_ptr<PinState> state) : state_(std::move(state)) {}
    bool SetActive(bool active) override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (active && state_->fail_activation) {
            return false;
        }
        if (!active && state_->reset_failures > 0) {
            --state_->reset_failures;
            return false;
        }
        state_->writes.push_back(active);
        state_->changed.notify_all();
        return true;
    }

private:
    std::shared_ptr<PinState> state_;
};

std::shared_ptr<AlarmOutputController> Controller(const std::shared_ptr<PinState>& state) {
    return std::make_shared<AlarmOutputController>(
        std::vector<AlarmOutputChannel>{{1, 42, true}},
        [state](const auto&) { return std::make_unique<FakePin>(state); });
}

nlohmann::json OutputNode() {
    return {
        {"actionId", "LA_AlarmOutput_Code"},
        {"actionName", "output"},
        {"flowActionId", "output-node"},
        {"preFlowActionId", "alarm-node"},
        {"configObject",
         {{"params", {{{"key", "outputChannel"}, {"value", "1"}}, {{"key", "duration"}, {"value", "5"}}}}}}};
}
}  // namespace

TEST_CASE("AlarmOut holds, extends and resets outputs without blocking alarms", "[alarm-output]") {
    auto state      = std::make_shared<PinState>();
    auto controller = Controller(state);
    SECTION("A pulse resets automatically") {
        REQUIRE(controller->Pulse(1, 30ms));
        REQUIRE(state->WaitForReset(2s));
        REQUIRE(state->Writes() == std::vector<bool>{true, false});
    }
    SECTION("Repeated alarms extend rather than toggle the output") {
        REQUIRE(controller->Pulse(1, 100ms));
        REQUIRE(controller->Pulse(1, 500ms));
        REQUIRE_FALSE(state->WaitForReset(200ms));
        REQUIRE(state->WaitForReset(2s));
        REQUIRE(state->Writes() == std::vector<bool>{true, false});
    }
    SECTION("A shorter pulse cannot shorten an existing hold") {
        REQUIRE(controller->Pulse(1, 500ms));
        REQUIRE(controller->Pulse(1, 20ms));
        REQUIRE_FALSE(state->WaitForReset(200ms));
        REQUIRE(state->WaitForReset(2s));
    }
    SECTION("Stop resets active outputs and rejects future alarms") {
        REQUIRE(controller->Pulse(1, 5s));
        controller->Stop();
        controller->Stop();
        REQUIRE(state->Writes() == std::vector<bool>{true, false});
        REQUIRE_FALSE(controller->Pulse(1, 5s));
    }
    SECTION("Failed resets are retried instead of forgetting an active output") {
        state->reset_failures = 1;
        REQUIRE(controller->Pulse(1, 10ms));
        REQUIRE_FALSE(state->WaitForReset(200ms));
        REQUIRE(state->WaitForReset(2s));
    }
    SECTION("Each channel has an independent deadline") {
        auto second = std::make_shared<PinState>();
        AlarmOutputController multiple({{1, 42, true}, {2, 43, false}}, [&](const auto& channel) {
            return std::make_unique<FakePin>(channel.id == 1 ? state : second);
        });
        REQUIRE(multiple.Pulse(1, 30ms));
        REQUIRE(multiple.Pulse(2, 600ms));
        REQUIRE(state->WaitForReset(2s));
        REQUIRE_FALSE(second->WaitForReset(100ms));
        multiple.Stop();
        REQUIRE(second->Writes() == std::vector<bool>{true, false});
    }
    SECTION("Unknown channels and invalid durations never activate a pin") {
        REQUIRE_FALSE(controller->Pulse(2, 5s));
        REQUIRE_FALSE(controller->Pulse(1, 0ms));
        REQUIRE_FALSE(controller->Pulse(1, 601s));
        REQUIRE(state->Writes().empty());
    }
    SECTION("Hardware activation failure is reported") {
        state->fail_activation = true;
        REQUIRE_FALSE(controller->Pulse(1, 5s));
        REQUIRE(state->Writes().empty());
    }
    SECTION("Missing hardware is not reported as a successful alarm") {
        AlarmOutputController missing({{1, 42, true}},
                                      [](const auto&) -> std::unique_ptr<AlarmOutputPin> { return {}; });
        REQUIRE_FALSE(missing.Pulse(1, 5s));
    }
}

TEST_CASE("AlarmOut parameter validation rejects malformed and ambiguous values", "[alarm-output]") {
    auto doc     = OutputNode();
    auto& params = doc["configObject"]["params"];
    LinkAgeAlarmOutputParam result;
    SECTION("Valid values") {
        REQUIRE(ParseAlarmOutputParam(doc.get<LinkAgeParamNode>(), result));
        REQUIRE(result.channel == 1);
        REQUIRE(result.duration == 5);
        return;
    }
    SECTION("Missing channel") {
        params.erase(0);
    }
    SECTION("Missing duration") {
        params.erase(1);
    }
    SECTION("Duplicate channel") {
        params.push_back(params[0]);
    }
    SECTION("Trailing text") {
        params[1]["value"] = "5seconds";
    }
    SECTION("Negative duration") {
        params[1]["value"] = "-1";
    }
    SECTION("Zero duration") {
        params[1]["value"] = "0";
    }
    SECTION("Too long") {
        params[1]["value"] = "601";
    }
    SECTION("Invalid channel") {
        params[0]["value"] = "65";
    }
    SECTION("Integer overflow") {
        params[0]["value"] = "999999999999999999999";
    }
    REQUIRE_FALSE(ParseAlarmOutputParam(doc.get<LinkAgeParamNode>(), result));
}

TEST_CASE("Only bound algorithm alarms reach the physical output action", "[alarm-output]") {
    nlohmann::json alarm = {
        {"actionId", "LA_AlarmData_Code"},
        {"flowActionId", "alarm-node"},
        {"preFlowActionId", "-1"},
        {"configObject",
         {{"params",
           {{{"key", "algs"}, {"value", R"([{"channelId":"camera","algorithmId":"algorithm"}])"}}}}}}};
    LinkageStrategyWorkflow workflow;
    workflow.workflow = {alarm.get<LinkAgeParamNode>(), OutputNode().get<LinkAgeParamNode>()};
    auto state        = std::make_shared<PinState>();
    auto controller   = Controller(state);
    LinkAgeTask task("alarm-output-test", workflow, controller);
    task.DoAlarm("other-camera", "algorithm");
    task.DoAlarm("camera", "other-algorithm");
    REQUIRE(state->Writes().empty());
    task.DoAlarm("camera", "algorithm");
    REQUIRE(state->Writes() == std::vector<bool>{true});
    controller->Stop();
    REQUIRE(state->Writes() == std::vector<bool>{true, false});
}

TEST_CASE("Board mapping requires explicit polarity and unique channels and pins", "[alarm-output]") {
    const auto file = std::filesystem::temp_directory_path() /
                      ("cosmo-alarm-output-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{file};
    nlohmann::json mapping = {{"outputs", {{{"id", 1}, {"gpio", 42}, {"activeLow", true}}}}};
    bool valid             = false;
    SECTION("Explicit mapping") {
        valid = true;
    }
    SECTION("Unknown polarity") {
        mapping["outputs"][0].erase("activeLow");
    }
    SECTION("Invalid GPIO type") {
        mapping["outputs"][0]["gpio"] = "42";
    }
    SECTION("Negative GPIO") {
        mapping["outputs"][0]["gpio"] = -1;
    }
    SECTION("Duplicate id") {
        mapping["outputs"].push_back({{"id", 1}, {"gpio", 43}, {"activeLow", false}});
    }
    SECTION("Duplicate physical output") {
        mapping["outputs"].push_back({{"id", 2}, {"gpio", 42}, {"activeLow", false}});
    }
    {
        std::ofstream output(file);
        output << mapping.dump();
    }
    const auto channels = LoadAlarmOutputChannels(file.string());
    REQUIRE(channels.size() == (valid ? 1 : 0));
    if (valid) {
        REQUIRE(channels[0].id == 1);
        REQUIRE(channels[0].gpio == 42);
        REQUIRE(channels[0].active_low);
    }
}
