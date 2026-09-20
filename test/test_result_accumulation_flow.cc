#include "catch_amalgamated.hpp"
#include "flow/logical/LogicalJudgment.h"
#include "flow/sensitivity/PosSaveSensitivity.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"

using namespace cosmo;

namespace {
ActionNode AccumulationNode(const std::string& id, bool logic) {
    ActionNode node;
    node.actionId     = logic ? "BA_90001" : "BA_20003";
    node.flowActionId = id;
    node.actionName   = id;
    if (logic) {
        node.configObject.condition.type         = LogicType::Greater;
        node.configObject.condition.keyLElements = {"aiOut", "completed", "confidence"};
        node.configObject.condition.keyRElements = {"aiParam", "completed", "confidence"};
    } else {
        MsgDynamicKeyValue mode;
        mode.key   = "inputMode";
        mode.value = "auto";
        node.configObject.params.push_back(mode);
    }
    return node;
}
AiDetectRstEl JudgmentTarget(int id, float score = 0.1f, int width = 8) {
    AiDetectRstEl target;
    target.trackId               = id;
    target.trackIdInfo           = "target-" + std::to_string(id);
    target.box                   = {0, 0, width, 8};
    target.confidence.label      = "completed";
    target.confidence.confidence = score;
    TargetAreaUnit area;
    area.area_id   = "area";
    area.area_name = "Area";
    target.areaSign.areas.push_back(area);
    return target;
}
struct MemoryContext {
    explicit MemoryContext(mem::MemoryPoolMng& pool) {
        mem::SetMemoryPoolContext(&pool);
    }
    ~MemoryContext() {
        mem::SetMemoryPoolContext(nullptr);
    }
};
struct AccumulationFlow {
    mem::MemoryPoolMng pool{std::make_unique<mem::AllocatorCpu>(), {384}};
    MemoryContext context{pool};
    ActionNode logic_node{AccumulationNode("judgment", true)};
    ActionNode accumulation_node{AccumulationNode("accumulation", false)};
    LogicalJudgment logic{"accumulation-task", logic_node};
    PosSaveSensitivity accumulation{"accumulation-task", accumulation_node};
    AlgTaskUnit judgment_output;
    AlgTaskUnit alarms;

    AccumulationFlow() {
        for (auto* sink : {&judgment_output, &alarms}) {
            sink->channel_id   = "channel";
            sink->task_id      = "accumulation-task";
            sink->fps          = -1;
            sink->flowActionId = sink == &alarms ? "alarm" : "accumulation";
            sink->actionId     = sink == &alarms ? "BA_00004" : "BA_20003";
            sink->que          = std::make_shared<AlgDataQueue<AlgDataPtr>>(sink->flowActionId);
        }
        REQUIRE(logic.RegistTaskQueue(judgment_output));
        REQUIRE(accumulation.RegistTaskQueue(alarms));
        SetThreshold("0.5");
    }
    ~AccumulationFlow() {
        logic.RemoveTaskQueue(judgment_output);
        accumulation.RemoveTaskQueue(alarms);
    }
    void SetThreshold(const std::string& value) {
        MsgDynamicKeyValue param;
        param.key   = "aiParam.completed.confidence";
        param.keys  = {"aiParam", "completed", "confidence"};
        param.value = value;
        std::vector<MsgDynamicKeyValue> params{param};
        logic.SetParam("channel", "accumulation-task", params);
    }
    VideoFramePtr Feed(int64_t index, int64_t time, std::vector<AiDetectRstEl> targets, bool complete = true,
                       int64_t stream = 1, bool run_judgment = true) {
        auto data           = std::make_shared<AlgData>();
        data->channelId     = "channel";
        data->taskId        = "accumulation-task";
        data->dataType      = AlgDataType::TaskDataClassify;
        data->bHaveTrack    = true;
        data->bHaveClassify = true;
        data->chanDataDec.frame =
            std::make_shared<media::VideoFrame>(16, 16, media::PixelFormat::PIXEL_I420, index, time);
        data->chanDataDec.frame->SetStreamIndex(stream);
        REQUIRE(data->chanDataDec.frame->Active());
        auto result                  = std::make_shared<DataDetTrackClassify>();
        result->dataType             = data->dataType;
        result->frameIndex           = index;
        result->timestamp            = time;
        result->streamIndex          = stream;
        result->observation_complete = complete;
        result->bHaveArea            = true;
        result->targets              = std::move(targets);
        data->SetTaskResult(data->dataType, result);
        if (run_judgment) {
            logic.HandFrame(data);
            REQUIRE(judgment_output.que->RestSize() == 1);
            auto judged = judgment_output.que->Pop();
            REQUIRE_FALSE(judged->GetTaskResult(data->dataType)->logic_context.empty());
            REQUIRE(result->logic_context.empty());  // A branch must not mutate its input.
            accumulation.HandFrame(judged);
        } else {
            accumulation.HandFrame(data);
        }
        return data->chanDataDec.frame;
    }
};
}  // namespace

TEST_CASE("Logical results accumulate per track and keep independent best snapshots",
          "[accumulation][flow]") {
    AccumulationFlow flow;
    auto first  = flow.Feed(1, 0, {JudgmentTarget(1, 0.1f, 12), JudgmentTarget(2)});
    auto second = flow.Feed(2, 1500, {JudgmentTarget(1), JudgmentTarget(2, 0.1f, 14)});
    flow.Feed(3, 3000, {JudgmentTarget(1), JudgmentTarget(2)});
    REQUIRE(flow.alarms.que->RestSize() == 0);
    flow.Feed(4, 4000, {});
    REQUIRE(flow.alarms.que->RestSize() == 2);
    for (int id : {1, 2}) {
        auto output       = flow.alarms.que->Pop();
        const auto& alarm = output->taskDataAlarm.alarmData->alarms.front();
        REQUIRE(alarm.trackId == id);
        REQUIRE(alarm.strTrackId == "target-" + std::to_string(id));
        REQUIRE_FALSE(alarm.haveRelated);
        REQUIRE(output->chanDataDec.frame == (id == 1 ? first : second));
    }
    flow.Feed(5, 5000, {});
    REQUIRE(flow.alarms.que->RestSize() == 0);
}

TEST_CASE("A satisfied judgment suppresses later negatives only for its own track", "[accumulation][flow]") {
    AccumulationFlow flow;
    flow.Feed(1, 0, {JudgmentTarget(1), JudgmentTarget(2)});
    flow.Feed(2, 1, {JudgmentTarget(1, 0.9f), JudgmentTarget(2)});
    flow.Feed(3, 1500, {JudgmentTarget(1), JudgmentTarget(2)});
    flow.Feed(4, 3000, {JudgmentTarget(1), JudgmentTarget(2)});
    flow.Feed(5, 4000, {});
    REQUIRE(flow.alarms.que->RestSize() == 1);
    REQUIRE(flow.alarms.que->Pop()->taskDataAlarm.alarmData->alarms.front().trackId == 2);
}

TEST_CASE("Missing filtered and failed judgments cannot supply negative evidence", "[accumulation][flow]") {
    AccumulationFlow flow;
    auto target       = JudgmentTarget(1);
    bool complete     = true;
    bool run_judgment = true;
    SECTION("missing model output") {
        target.confidence.label = "person";
    }
    SECTION("filtered target") {
        target.bFilter = true;
    }
    SECTION("outside configured areas") {
        target.areaSign.areas.clear();
    }
    SECTION("failed inference") {
        complete = false;
    }
    SECTION("unjudged default false") {
        run_judgment = false;
    }
    flow.Feed(1, 0, {target}, complete, 1, run_judgment);
    flow.Feed(2, 1500, {target}, complete, 1, run_judgment);
    flow.Feed(3, 3000, {target}, complete, 1, run_judgment);
    flow.Feed(4, 4000, {});
    REQUIRE(flow.alarms.que->RestSize() == 0);
}

TEST_CASE("Observation interruptions and configuration changes discard pending negative conclusions",
          "[accumulation][flow]") {
    AccumulationFlow flow;
    flow.Feed(1, 0, {JudgmentTarget(1)});
    flow.Feed(2, 1500, {JudgmentTarget(1)});
    flow.Feed(3, 3000, {JudgmentTarget(1)});
    SECTION("failed frame") {
        flow.Feed(4, 3500, {}, false);
    }
    SECTION("unjudged packet") {
        flow.Feed(4, 3500, {}, true, 1, false);
    }
    SECTION("invalid packet") {
        flow.accumulation.HandFrame(nullptr);
    }
    SECTION("changed judgment threshold") {
        flow.SetThreshold("0.6");
    }
    SECTION("changed accumulation parameters") {
        MsgDynamicKeyValue param;
        param.key   = "param.observationIntervalMs";
        param.value = "500";
        std::vector<MsgDynamicKeyValue> params{param};
        flow.accumulation.ModifyParam("channel", "accumulation-task", params);
    }
    SECTION("stream restarted") {
        flow.Feed(1, 3500, {}, true, 2);
    }
    SECTION("long input gap") {
        flow.Feed(4, 20000, {});
    }
    flow.Feed(5, 4500, {});
    REQUIRE(flow.alarms.que->RestSize() == 0);
}

TEST_CASE("Legacy and canonical observation parameters apply with canonical precedence",
          "[accumulation][flow]") {
    AccumulationFlow flow;
    MsgDynamicKeyValue count, interval;
    count.key      = "param.minValidFaceCount";
    count.value    = "4";
    interval.key   = "param.faceSampleIntervalMs";
    interval.value = "200";
    std::vector<MsgDynamicKeyValue> params{count, interval};
    bool expect_alarm = false;
    SECTION("legacy count still controls the threshold") {}
    SECTION("canonical count wins even when legacy appears later") {
        auto canonical  = count;
        canonical.key   = "param.minValidObservationCount";
        canonical.value = "3";
        params.insert(params.begin(), canonical);
        expect_alarm = true;
    }
    SECTION("canonical sampling interval wins even when legacy appears later") {
        params.front().value = "3";
        auto canonical       = interval;
        canonical.key        = "param.observationIntervalMs";
        canonical.value      = "10000";
        params.insert(params.begin(), canonical);
    }
    flow.accumulation.SetParam("channel", "accumulation-task", params);
    flow.Feed(1, 0, {JudgmentTarget(1)});
    flow.Feed(2, 1500, {JudgmentTarget(1)});
    flow.Feed(3, 3000, {JudgmentTarget(1)});
    flow.Feed(4, 4000, {});
    REQUIRE(flow.alarms.que->RestSize() == (expect_alarm ? 1 : 0));
}
