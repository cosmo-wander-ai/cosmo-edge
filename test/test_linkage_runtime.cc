#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "linkage/LinkAgeAlarm.h"
#include "linkage/LinkAgeBase.h"
#include "linkage/LinkAgeBaseCommon.h"
#include "linkage/LinkAgeTask.h"
#include "mock/MockAudioService.h"
#include "support/ScopedServiceOverride.h"
#include "util/Keys.h"

namespace cosmo::linkage {
namespace {

    using trompeloeil::_;

    struct LinkageAudioDependency {
        test::MockAudioService audioSvc;
        test::ScopedServiceOverride<service::IAudioService> registration{audioSvc};
    };

    MsgDynamicKeyValue MakeParameter(std::string_view key, std::string value) {
        MsgDynamicKeyValue parameter;
        parameter.key   = std::string(key);
        parameter.value = std::move(value);
        return parameter;
    }

    LinkAgeParamNode MakeAlarmNode(std::string flow_action_id, std::string pre_flow_action_id,
                                   std::string_view action_id      = kLaAlarmDataCode,
                                   std::string_view algorithms_key = kKeyLinkageAlgs) {
        LinkAgeParamNode node;
        node.action_id       = action_id;
        node.action_name     = "alarm";
        node.flowActionId    = std::move(flow_action_id);
        node.preFlowActionId = std::move(pre_flow_action_id);
        node.config_object.params.push_back(
            MakeParameter(algorithms_key, R"([{"channelId":"channel-1","algorithmId":"algorithm-1"}])"));
        return node;
    }

    LinkAgeParamNode MakeAudioNode(std::string flow_action_id, std::string pre_flow_action_id,
                                   std::string device_id, std::string audio_file_id,
                                   std::string_view action_id  = kLaAudioDeviceCode,
                                   std::string_view device_key = kKeyLinkageAudioDeviceId,
                                   std::string_view text_key   = kKeyLinkageAudioDeviceText) {
        LinkAgeParamNode node;
        node.action_id            = action_id;
        node.action_name          = "audio";
        node.flowActionId         = std::move(flow_action_id);
        node.preFlowActionId      = std::move(pre_flow_action_id);
        node.config_object.params = {
            MakeParameter(device_key, std::move(device_id)),
            MakeParameter(kKeyStrageAudioDeviceOperation, "1"),
            MakeParameter(kKeyStrageAudioDeviceData, std::move(audio_file_id)),
            MakeParameter(text_key, ""),
        };
        return node;
    }

    LinkageStrategyWorkflow MakeBranchedWorkflow() {
        LinkageStrategyWorkflow strategy;
        strategy.workflow.push_back(MakeAlarmNode("alarm-root", std::string(key::alg::ACTION_ROOT_VALUE)));
        strategy.workflow.push_back(
            MakeAudioNode("audio-first", "alarm-root", "speaker-first", "file-first"));
        strategy.workflow.push_back(
            MakeAudioNode("audio-sibling", "alarm-root", "speaker-sibling", "file-sibling"));
        strategy.workflow.push_back(
            MakeAudioNode("audio-grandchild", "audio-first", "speaker-grandchild", "file-grandchild"));
        return strategy;
    }

}  // namespace

TEST_CASE("Linkage runtime classifies compatible action and parameter identifiers", "[linkage-runtime]") {
    REQUIRE(ClassifyLinkAgeActionId(kLaAlarmDataCode) == LinkAgeActionKind::kAlarm);
    REQUIRE(ClassifyLinkAgeActionId(kLaAlarmDataLegacyCode) == LinkAgeActionKind::kAlarm);
    REQUIRE(ClassifyLinkAgeActionId(kLaAudioDeviceCode) == LinkAgeActionKind::kAudioDevice);
    REQUIRE(ClassifyLinkAgeActionId(kLaAudioDeviceLegacyCode) == LinkAgeActionKind::kAudioDevice);
    REQUIRE(ClassifyLinkAgeActionId("unsupported") == LinkAgeActionKind::kUnsupported);

    REQUIRE(IsAlarmAlgorithmsKey(kKeyLinkageAlgs));
    REQUIRE(IsAlarmAlgorithmsKey(kKeyStrageAlgs));
    REQUIRE_FALSE(IsAlarmAlgorithmsKey("unrelated"));

    REQUIRE(IsAudioDeviceIdKey(kKeyLinkageAudioDeviceId));
    REQUIRE(IsAudioDeviceIdKey(kKeyStrageAudioDeviceId));
    REQUIRE_FALSE(IsAudioDeviceIdKey("unrelated"));

    REQUIRE(IsAudioTextKey(kKeyLinkageAudioDeviceText));
    REQUIRE(IsAudioTextKey(kKeyStrageAudioDeviceText));
    REQUIRE_FALSE(IsAudioTextKey("unrelated"));
}

TEST_CASE("LinkAgeBase retains a construction-time action snapshot", "[linkage-runtime]") {
    auto action          = MakeAlarmNode("flow-original", "parent-original");
    action.action_name   = "name-original";
    const auto action_id = action.action_id;
    LinkAgeBase snapshot(action);

    action.action_id       = "changed-action";
    action.action_name     = "changed-name";
    action.flowActionId    = "changed-flow";
    action.preFlowActionId = "changed-parent";
    action.config_object.params.clear();

    REQUIRE(snapshot.GetActionId() == action_id);
    REQUIRE(snapshot.GetName() == "name-original");
    REQUIRE(snapshot.GetFlowActionId() == "flow-original");
    REQUIRE(snapshot.GetPreFlowActionId() == "parent-original");
}

TEST_CASE("Linkage JSON preserves optional-field semantics", "[linkage-runtime]") {
    LinkAgeParamNode action = MakeAlarmNode("old-flow", "old-parent");
    action.action_name      = "old-name";

    const nlohmann::json missing_optional = {
        {"actionId", "new-action"}, {"flowActionId", "new-flow"}, {"preFlowActionId", "new-parent"}};
    missing_optional.get_to(action);
    REQUIRE(action.action_name == "old-name");
    REQUIRE(action.config_object.params.size() == 1);

    auto null_optional            = missing_optional;
    null_optional["actionName"]   = nullptr;
    null_optional["configObject"] = nullptr;
    null_optional.get_to(action);
    REQUIRE(action.action_name == "old-name");
    REQUIRE(action.config_object.params.size() == 1);

    auto present_optional            = missing_optional;
    present_optional["actionName"]   = "new-name";
    present_optional["configObject"] = {
        {"params", nlohmann::json::array({{{"key", "algs"}, {"value", "[]"}}})}};
    present_optional.get_to(action);
    REQUIRE(action.action_name == "new-name");
    REQUIRE(action.config_object.params.size() == 1);
    REQUIRE(action.config_object.params.front().key.ToRefString() == "algs");

    auto missing_action_id = missing_optional;
    missing_action_id.erase("actionId");
    REQUIRE_THROWS_AS(missing_action_id.get_to(action), nlohmann::json::out_of_range);

    auto missing_flow_action_id = missing_optional;
    missing_flow_action_id.erase("flowActionId");
    REQUIRE_THROWS_AS(missing_flow_action_id.get_to(action), nlohmann::json::out_of_range);

    auto missing_pre_flow_action_id = missing_optional;
    missing_pre_flow_action_id.erase("preFlowActionId");
    REQUIRE_THROWS_AS(missing_pre_flow_action_id.get_to(action), nlohmann::json::out_of_range);

    LinkAgeAlarmTaskUnit alarm_task{"old-channel", "old-algorithm"};
    nlohmann::json::object().get_to(alarm_task);
    REQUIRE(alarm_task.channel_id == "old-channel");
    REQUIRE(alarm_task.algorithm_id == "old-algorithm");
    nlohmann::json({{"channelId", nullptr}, {"algorithmId", nullptr}}).get_to(alarm_task);
    REQUIRE(alarm_task.channel_id == "old-channel");
    REQUIRE(alarm_task.algorithm_id == "old-algorithm");
    nlohmann::json({{"channelId", "new-channel"}, {"algorithmId", "new-algorithm"}}).get_to(alarm_task);
    REQUIRE(alarm_task.channel_id == "new-channel");
    REQUIRE(alarm_task.algorithm_id == "new-algorithm");

    LinkageStrategyWorkflow workflow;
    workflow.workflow.push_back(action);
    nlohmann::json::object().get_to(workflow);
    REQUIRE(workflow.workflow.size() == 1);
    nlohmann::json({{"workflow", nullptr}}).get_to(workflow);
    REQUIRE(workflow.workflow.size() == 1);
    nlohmann::json({{"workflow", nlohmann::json::array()}}).get_to(workflow);
    REQUIRE(workflow.workflow.empty());
}

TEST_CASE("LinkAgeTask queries sibling and grandchild audio actions", "[linkage-runtime]") {
    auto strategy = MakeBranchedWorkflow();
    LinkAgeTask task("branched", strategy);

    REQUIRE(task.IsAudioDeviceInUse("speaker-first"));
    REQUIRE(task.IsAudioDeviceInUse("speaker-sibling"));
    REQUIRE(task.IsAudioDeviceInUse("speaker-grandchild"));
    REQUIRE_FALSE(task.IsAudioDeviceInUse("speaker-missing"));

    REQUIRE(task.IsAudioFileInUse("file-first"));
    REQUIRE(task.IsAudioFileInUse("file-sibling"));
    REQUIRE(task.IsAudioFileInUse("file-grandchild"));
    REQUIRE_FALSE(task.IsAudioFileInUse("file-missing"));
}

TEST_CASE("LinkAgeTask preserves alarm gating and depth-first sibling order", "[linkage-runtime]") {
    LinkageAudioDependency mocks;
    auto strategy = MakeBranchedWorkflow();
    LinkAgeTask task("ordered", strategy);

    SECTION("matching root executes descendants in order") {
        std::vector<std::string> played_devices;
        REQUIRE_CALL(mocks.audioSvc, PlayAudioDevice(_))
            .LR_SIDE_EFFECT(played_devices.push_back(_1.devId))
            .RETURN(true)
            .TIMES(3);

        task.DoAlarm("channel-1", "algorithm-1");

        REQUIRE(played_devices ==
                std::vector<std::string>{"speaker-first", "speaker-grandchild", "speaker-sibling"});
    }

    SECTION("non-matching root suppresses all descendants") {
        FORBID_CALL(mocks.audioSvc, PlayAudioDevice(_));
        task.DoAlarm("other-channel", "other-algorithm");
    }
}

TEST_CASE("Linkage runtime parses legacy text-play parameters", "[linkage-runtime]") {
    LinkageAudioDependency mocks;
    auto alarm = MakeAlarmNode("alarm-root", std::string(key::alg::ACTION_ROOT_VALUE), kLaAlarmDataLegacyCode,
                               kKeyStrageAlgs);
    auto audio = MakeAudioNode("audio", "alarm-root", "legacy-speaker", "unused-audio",
                               kLaAudioDeviceLegacyCode, kKeyStrageAudioDeviceId, kKeyStrageAudioDeviceText);
    audio.config_object.params[1].value = std::string("2");
    audio.config_object.params[3].value = std::string("legacy text");

    LinkageStrategyWorkflow strategy;
    strategy.workflow = {std::move(alarm), std::move(audio)};
    LinkAgeTask task("legacy-text", strategy);

    AudioDevicePlay captured;
    REQUIRE_CALL(mocks.audioSvc, PlayAudioDevice(_)).LR_SIDE_EFFECT(captured = _1).RETURN(true);
    task.DoAlarm("channel-1", "algorithm-1");

    REQUIRE(captured.devId == "legacy-speaker");
    REQUIRE(captured.playType == AudioDevicePlayType::AudioDevicePlayTypeTextPlay);
    REQUIRE(captured.data == "legacy text");
}

TEST_CASE("Linkage runtime keeps last matching compatibility parameter", "[linkage-runtime]") {
    LinkageAudioDependency mocks;
    auto alarm = MakeAlarmNode("alarm-root", std::string(key::alg::ACTION_ROOT_VALUE));
    alarm.config_object.params.push_back(
        MakeParameter(kKeyStrageAlgs, R"([{"channelId":"other-channel","algorithmId":"other-algorithm"}])"));
    auto audio = MakeAudioNode("audio", "alarm-root", "speaker-first", "file-first");
    audio.config_object.params.push_back(MakeParameter(kKeyStrageAudioDeviceId, "speaker-last"));

    LinkageStrategyWorkflow strategy;
    strategy.workflow = {std::move(alarm), std::move(audio)};
    LinkAgeTask task("compatibility-precedence", strategy);

    REQUIRE_FALSE(task.IsAudioDeviceInUse("speaker-first"));
    REQUIRE(task.IsAudioDeviceInUse("speaker-last"));

    std::vector<std::string> played_devices;
    REQUIRE_CALL(mocks.audioSvc, PlayAudioDevice(_))
        .LR_SIDE_EFFECT(played_devices.push_back(_1.devId))
        .RETURN(true);
    task.DoAlarm("channel-1", "algorithm-1");
    REQUIRE(played_devices.empty());
    task.DoAlarm("other-channel", "other-algorithm");
    REQUIRE(played_devices == std::vector<std::string>{"speaker-last"});
}

}  // namespace cosmo::linkage
