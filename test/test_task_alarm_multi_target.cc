// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "flow/alarm/TaskAlarm.h"
#include "mock/MockAlarmRecordService.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockCameraService.h"
#include "mock/MockConfigReadService.h"
#include "service/ai/IVisualDecisionService.h"
#include "service/event/IEventNotifier.h"
#include "support/MockDefaults.h"
#include "support/ScopedServiceOverride.h"

namespace {

struct TaskAlarmDependencies {
    cosmo::test::MockAlarmRecordService alarmRecordSvc;
    cosmo::test::MockCameraService cameraSvc;
    cosmo::test::MockConfigReadService configReadSvc;
    cosmo::test::MockAppInfoService appInfoSvc;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::ScopedServiceOverride<cosmo::service::IAlarmRecordService> alarmRecord{alarmRecordSvc};
    cosmo::test::ScopedServiceOverride<cosmo::service::ICameraChannelQuery> cameraQuery{cameraSvc};
    cosmo::test::ScopedServiceOverride<cosmo::service::IConfigReadService> configRead{configReadSvc};
    cosmo::test::ScopedServiceOverride<cosmo::service::IOverviewConfig> overviewConfig{appInfoSvc};

    TaskAlarmDependencies() {
        cosmo::test::AllowOverviewDisabled(appInfoSvc, expectations);
    }
};

class CapturingEventNotifier final : public cosmo::service::IEventNotifier {
public:
    bool InitializeWebSocket(const std::string&, int) override {
        return true;
    }
    void ShutdownWebSocket() override {}
    void WebSocketEventPush(cosmo::CMsgOnEventsReq& eventData) override {
        websocketEvents.push_back(eventData);
    }
    bool NotifyComplete(cosmo::CMsgOnCompleteReq&, cosmo::CMsgOnCompleteRsp&) override {
        return false;
    }
    bool NotifyInfo(cosmo::CMsgOnInfoReq&, cosmo::CMsgonInfoRsp&) override {
        return false;
    }
    bool GetVideoPlayUrl(cosmo::CMsgGetVideoPlayReq&, cosmo::CMsgGetVideoPlayRsp&) override {
        return false;
    }
    void SetEventPostQue(cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>&) override {}
    void ClearEventPostQue(const cosmo::AsyncQueue<cosmo::CMsgOnEventsReq>&) override {}
    void SetCollectPostQue(cosmo::AsyncQueue<cosmo::CMsgCollectRptReq>&) override {}
    void SetFaceEventPostQue(cosmo::AsyncQueue<cosmo::CMsgFaceEventReq>&) override {}
    void EventPush(cosmo::CMsgOnEventsReq& msg) override {
        httpEvents.push_back(msg);
    }
    void FaceEventPush(cosmo::CMsgFaceEventReq&) override {}
    void CollectPush(cosmo::CMsgCollectRptReq&) override {}

    std::vector<cosmo::CMsgOnEventsReq> websocketEvents;
    std::vector<cosmo::CMsgOnEventsReq> httpEvents;
};

cosmo::DataAlarmUnit MakeTaskAlarmUnit(int trackId, std::string trackIdText, cosmo::util::Box box) {
    cosmo::DataAlarmUnit unit;
    unit.flowActionId = "area-flow";
    unit.areaId       = "area-1";
    unit.areaName     = "Area 1";
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

cosmo::AlgDataPtr MakeTaskAlarmFrame() {
    auto data                                  = std::make_shared<cosmo::AlgData>();
    data->taskDataAlarm.alarmData              = std::make_shared<cosmo::DataAlarm>();
    data->taskDataAlarm.alarmData->multiAlarms = 1;
    data->taskDataAlarm.alarmData->alarms.push_back(MakeTaskAlarmUnit(9, "track-9", {300, 80, 60, 160}));
    data->taskDataAlarm.alarmData->alarms.push_back(MakeTaskAlarmUnit(4, "track-4", {100, 60, 80, 180}));
    return data;
}

cosmo::AlgDataPtr MakeUntrackedTaskAlarmFrame() {
    auto data                                  = std::make_shared<cosmo::AlgData>();
    data->taskDataAlarm.alarmData              = std::make_shared<cosmo::DataAlarm>();
    data->taskDataAlarm.alarmData->multiAlarms = 1;
    data->taskDataAlarm.alarmData->alarms.push_back(MakeTaskAlarmUnit(-1, "", {100, 60, 80, 180}));
    data->taskDataAlarm.alarmData->alarms.push_back(MakeTaskAlarmUnit(-1, "", {300, 80, 60, 160}));
    data->taskDataAlarm.alarmData->alarms.push_back(MakeTaskAlarmUnit(-1, "", {500, 100, 70, 150}));
    return data;
}

}  // namespace

TEST_CASE("Typed visual ROI records follow the actual merged alarm record and dispatch",
          "[visual-flow][alarm]") {
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> registration(notifier);
    REQUIRE_CALL(mocks.cameraSvc, GetChannelName("channel")).RETURN("Camera");
    REQUIRE_CALL(mocks.configReadSvc, IsNetworkModel()).RETURN(true);
    std::string stored;
    REQUIRE_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_))
        .LR_SIDE_EFFECT(stored = _1.property)
        .RETURN(true);
    cosmo::ActionNode action;
    action.flowActionId = "alarm-flow";
    cosmo::TaskAlarm alarm("channel", "task", action);
    auto frame = MakeTaskAlarmFrame();
    auto run   = std::make_shared<cosmo::service::VisualDecisionRun>("task", "epoch", "revision");
    for (auto& unit : frame->taskDataAlarm.alarmData->alarms) {
        unit.bLlmPrejudged = true;
        unit.visualRun     = run;
        unit.visualJudgments.push_back(
            {{"roi_id", unit.strTrackId},
             {"items", nlohmann::json::array({{{"status", "completed"}, {"top1", "false"}},
                                              {{"status", "unknown"}, {"reason", "deadline_exceeded"}}})}});
    }
    alarm.HandFrame(frame);
    REQUIRE(notifier.httpEvents.size() == 1);
    const auto& event = notifier.httpEvents.front();
    REQUIRE(event.bHaveProperty);
    REQUIRE(event.property.visualJudgments.size() == 2);
    CHECK(event.property.visualJudgments[0]["roi_id"] == "track-4");
    CHECK(event.property.visualJudgments[1]["roi_id"] == "track-9");
    auto record = nlohmann::json::parse(stored);
    CHECK(record["visualJudgments"] == event.property.visualJudgments);
    auto reloaded = nlohmann::json(event).get<cosmo::CMsgOnEventsReq>();
    CHECK(reloaded.property.visualJudgments == event.property.visualJudgments);
}

TEST_CASE("Stopped visual runs cannot publish queued alarm units", "[visual-flow][alarm]") {
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> registration(notifier);
    FORBID_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_));
    cosmo::ActionNode action;
    action.flowActionId = "alarm-flow";
    cosmo::TaskAlarm alarm("channel", "task", action);
    auto frame = MakeTaskAlarmFrame();
    auto run   = std::make_shared<cosmo::service::VisualDecisionRun>("task", "epoch", "revision");
    for (auto& unit : frame->taskDataAlarm.alarmData->alarms) {
        unit.bLlmPrejudged = true;
        unit.visualRun     = run;
    }
    run->Invalidate();
    alarm.HandFrame(frame);
    CHECK(notifier.httpEvents.empty());
    CHECK(alarm.GetAlarmRealCnt() == 0);
}

TEST_CASE("Visual invalidation during event construction does not consume suppression or alarm budgets",
          "[visual-flow][alarm][epoch]") {
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> registration(notifier);
    auto oldRun = std::make_shared<cosmo::service::VisualDecisionRun>("task", "old", "revision");
    int builds  = 0;
    REQUIRE_CALL(mocks.cameraSvc, GetChannelName("channel"))
        .TIMES(2)
        .LR_SIDE_EFFECT(if (++builds == 1) oldRun->Invalidate())
        .RETURN("Camera");
    REQUIRE_CALL(mocks.configReadSvc, IsNetworkModel()).RETURN(true);
    REQUIRE_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_)).RETURN(true);
    cosmo::ActionNode action;
    action.flowActionId = "alarm-flow";
    cosmo::TaskAlarm alarm("channel", "task", action);
    std::vector<cosmo::MsgDynamicKeyValue> params;
    for (const auto& [key, value] :
         std::vector<std::pair<std::string, std::string>>{{"targetAlarmCount", "1"},
                                                          {"alarmInterval", "300"},
                                                          {"targetAlarmInterval", "300"},
                                                          {"restrainSwitch", "1"},
                                                          {"overlapRate", "0.5"},
                                                          {"restrainTime", "1"}}) {
        cosmo::MsgDynamicKeyValue p;
        p.key   = "param." + key;
        p.keys  = {"param", key};
        p.value = value;
        params.push_back(p);
    }
    // Legacy TaskAlarm setters return false even when applied; verify behavior below.
    alarm.SetParam("channel", "task", params);
    auto send = [&](std::shared_ptr<cosmo::service::VisualDecisionRun> run) {
        auto frame = MakeTaskAlarmFrame();
        frame->taskDataAlarm.alarmData->alarms.resize(1);
        auto& unit         = frame->taskDataAlarm.alarmData->alarms.front();
        unit.bLlmPrejudged = true;
        unit.visualRun     = std::move(run);
        alarm.HandFrame(frame);
    };
    send(oldRun);
    REQUIRE(alarm.GetAlarmRealCnt() == 0);
    REQUIRE(notifier.httpEvents.empty());
    send(std::make_shared<cosmo::service::VisualDecisionRun>("task", "new", "revision-2"));
    CHECK(alarm.GetAlarmRealCnt() == 1);
    CHECK(notifier.httpEvents.size() == 1);
}

TEST_CASE("TaskAlarm emits one event with all same-frame abnormal targets", "[alarm][batch][event]") {
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> notifierRegistration(notifier);

    REQUIRE_CALL(mocks.cameraSvc, GetChannelName("channel")).RETURN("Camera");
    REQUIRE_CALL(mocks.configReadSvc, IsNetworkModel()).RETURN(true);
    REQUIRE_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_)).RETURN(true);

    cosmo::ActionNode action;
    action.actionId     = "alarm-action";
    action.actionName   = "Alarm";
    action.flowActionId = "alarm-flow";
    cosmo::TaskAlarm alarm("channel", "task", action);

    cosmo::MsgDynamicKeyValue targetCount;
    targetCount.key   = "param.targetAlarmCount";
    targetCount.value = "1";
    targetCount.keys  = {"param", "targetAlarmCount"};
    std::vector<cosmo::MsgDynamicKeyValue> params{targetCount};
    alarm.SetParam("channel", "task", params);

    alarm.HandFrame(MakeTaskAlarmFrame());

    REQUIRE(alarm.GetAlarmRealCnt() == 1);
    REQUIRE(notifier.httpEvents.size() == 1);
    REQUIRE(notifier.websocketEvents.size() == 1);
    REQUIRE(notifier.httpEvents[0].targets.size() == 2);
    CHECK(notifier.httpEvents[0].recordId == "track-4");
    CHECK(notifier.httpEvents[0].targets[0].trackId == "track-4");
    CHECK(notifier.httpEvents[0].targets[1].trackId == "track-9");

    // Both target counters must be updated by the first batch. With a per-target
    // limit of one, the next frame must not leak a second event for either target.
    alarm.HandFrame(MakeTaskAlarmFrame());
    CHECK(alarm.GetAlarmRealCnt() == 1);
    CHECK(notifier.httpEvents.size() == 1);
    CHECK(notifier.websocketEvents.size() == 1);
}

TEST_CASE("TaskAlarm emits one event for any number of untracked same-frame targets",
          "[alarm][batch][event][untracked]") {
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> notifierRegistration(notifier);

    REQUIRE_CALL(mocks.cameraSvc, GetChannelName("channel")).RETURN("Camera");
    REQUIRE_CALL(mocks.configReadSvc, IsNetworkModel()).RETURN(true);
    REQUIRE_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_)).RETURN(true);

    cosmo::ActionNode action;
    action.actionId     = "alarm-action";
    action.actionName   = "Alarm";
    action.flowActionId = "alarm-flow";
    cosmo::TaskAlarm alarm("channel", "task", action);

    alarm.HandFrame(MakeUntrackedTaskAlarmFrame());

    REQUIRE(alarm.GetAlarmRealCnt() == 1);
    REQUIRE(notifier.httpEvents.size() == 1);
    REQUIRE(notifier.websocketEvents.size() == 1);
    REQUIRE(notifier.httpEvents[0].targets.size() == 3);
    CHECK(notifier.httpEvents[0].recordId.empty());
    CHECK(notifier.httpEvents[0].targets[0].box.x == 100);
    CHECK(notifier.httpEvents[0].targets[1].box.x == 300);
    CHECK(notifier.httpEvents[0].targets[2].box.x == 500);
}

TEST_CASE("Associated alarm flows preserve every visual audit and configuration fence",
          "[visual-flow][alarm][association]") {
    const bool untrackedPrimary = GENERATE(false, true);
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> registration(notifier);
    REQUIRE_CALL(mocks.cameraSvc, GetChannelName("channel")).RETURN("Camera");
    REQUIRE_CALL(mocks.configReadSvc, IsNetworkModel()).RETURN(true);
    std::string stored;
    REQUIRE_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_))
        .LR_SIDE_EFFECT(stored = _1.property)
        .RETURN(true);
    cosmo::ActionNode action;
    cosmo::TaskAlarm alarm("channel", "task", action);
    auto frame       = MakeTaskAlarmFrame();
    auto& data       = *frame->taskDataAlarm.alarmData;
    data.multiAlarms = 2;
    for (size_t i = 0; i < data.alarms.size(); ++i) {
        auto& unit         = data.alarms[i];
        unit.flowActionId  = "flow-" + std::to_string(i);
        unit.bLlmPrejudged = true;
        unit.visualRun =
            std::make_shared<cosmo::service::VisualDecisionRun>("task", unit.flowActionId, "revision");
        unit.visualJudgments.push_back({{"roi_id", unit.strTrackId}, {"flow_action_id", unit.flowActionId}});
    }
    if (untrackedPrimary)
        data.alarms.front().trackId = -1;
    alarm.HandFrame(frame);
    REQUIRE(notifier.httpEvents.size() == 1);
    REQUIRE(notifier.httpEvents.front().property.visualJudgments.size() == 2);
    std::set<std::string> flows;
    for (const auto& record : notifier.httpEvents.front().property.visualJudgments)
        flows.insert(record.at("flow_action_id"));
    CHECK(flows == std::set<std::string>{"flow-0", "flow-1"});
    CHECK(nlohmann::json::parse(stored)["visualJudgments"] ==
          notifier.httpEvents.front().property.visualJudgments);
}

TEST_CASE("Invalidating either associated visual flow cancels publication without consuming target budgets",
          "[visual-flow][alarm][association][epoch]") {
    const int cancelled = GENERATE(0, 1);
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> registration(notifier);
    auto first        = std::make_shared<cosmo::service::VisualDecisionRun>("task", "first", "revision");
    auto second       = std::make_shared<cosmo::service::VisualDecisionRun>("task", "second", "revision");
    auto cancelledRun = cancelled == 0 ? first : second;
    int builds        = 0;
    REQUIRE_CALL(mocks.cameraSvc, GetChannelName("channel"))
        .TIMES(2)
        .LR_SIDE_EFFECT(if (++builds == 1) cancelledRun->Invalidate())
        .RETURN("Camera");
    REQUIRE_CALL(mocks.configReadSvc, IsNetworkModel()).RETURN(true);
    REQUIRE_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_)).RETURN(true);
    cosmo::ActionNode action;
    cosmo::TaskAlarm alarm("channel", "task", action);
    cosmo::MsgDynamicKeyValue limit;
    limit.key   = "param.targetAlarmCount";
    limit.keys  = {"param", "targetAlarmCount"};
    limit.value = "1";
    std::vector<cosmo::MsgDynamicKeyValue> parameters{limit};
    alarm.SetParam("channel", "task", parameters);
    auto send = [&] {
        auto frame                  = MakeTaskAlarmFrame();
        auto& data                  = *frame->taskDataAlarm.alarmData;
        data.multiAlarms            = 2;
        data.alarms[0].flowActionId = "first";
        data.alarms[1].flowActionId = "second";
        data.alarms[0].visualRun    = first;
        data.alarms[1].visualRun    = second;
        for (auto& unit : data.alarms) {
            unit.bLlmPrejudged = true;
            unit.visualJudgments.push_back({{"roi_id", unit.strTrackId}});
        }
        alarm.HandFrame(frame);
    };
    send();
    CHECK(notifier.httpEvents.empty());
    CHECK(alarm.GetAlarmRealCnt() == 0);
    first  = std::make_shared<cosmo::service::VisualDecisionRun>("task", "fresh-first", "revision");
    second = std::make_shared<cosmo::service::VisualDecisionRun>("task", "fresh-second", "revision");
    send();
    CHECK(notifier.httpEvents.size() == 1);
    CHECK(alarm.GetAlarmRealCnt() == 1);
}

TEST_CASE("A stopped secondary visual contributor prevents an otherwise unguarded alarm publication",
          "[visual-flow][alarm][association]") {
    TaskAlarmDependencies mocks;
    CapturingEventNotifier notifier;
    cosmo::test::ScopedServiceOverride<cosmo::service::IEventNotifier> registration(notifier);
    FORBID_CALL(mocks.alarmRecordSvc, Insert(trompeloeil::_));
    FORBID_CALL(mocks.cameraSvc, GetChannelName(trompeloeil::_));
    cosmo::ActionNode action;
    cosmo::TaskAlarm alarm("channel", "task", action);
    auto frame                                             = MakeTaskAlarmFrame();
    frame->taskDataAlarm.alarmData->multiAlarms            = 2;
    frame->taskDataAlarm.alarmData->alarms[0].flowActionId = "first";
    auto& second                                           = frame->taskDataAlarm.alarmData->alarms[1];
    second.flowActionId                                    = "second";
    second.visualRun = std::make_shared<cosmo::service::VisualDecisionRun>("task", "stopped", "revision");
    second.visualRun->Invalidate();
    alarm.HandFrame(frame);
    CHECK(notifier.httpEvents.empty());
    CHECK(alarm.GetAlarmRealCnt() == 0);
}
