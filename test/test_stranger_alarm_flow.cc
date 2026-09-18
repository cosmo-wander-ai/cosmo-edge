#include "catch_amalgamated.hpp"
#include "flow/sensitivity/PosSaveSensitivity.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"

using namespace cosmo;

namespace {
ActionNode StrangerAction() {
    ActionNode node;
    node.actionId     = "BA_20003";
    node.flowActionId = "stranger-evidence";
    node.actionName   = "stranger-evidence";
    MsgDynamicKeyValue mode;
    mode.key   = "inputMode";
    mode.value = "recognition";
    node.configObject.params.push_back(mode);
    return node;
}
class TestableEvidence : public PosSaveSensitivity {
public:
    using PosSaveSensitivity::PosSaveSensitivity;
    using PosSaveSensitivity::ResetStateOnRestart;
};
struct PoolScope {
    explicit PoolScope(mem::MemoryPoolMng& pool) {
        mem::SetMemoryPoolContext(&pool);
    }
    ~PoolScope() {
        mem::SetMemoryPoolContext(nullptr);
    }
};
struct StrangerFlow {
    mem::MemoryPoolMng pool{std::make_unique<mem::AllocatorCpu>(), {384}};
    PoolScope pool_scope{pool};
    ActionNode node{StrangerAction()};
    TestableEvidence action{"stranger-task", node};
    AlgTaskUnit sink;
    StrangerFlow() {
        sink.channel_id   = "channel";
        sink.task_id      = "stranger-task";
        sink.actionId     = "BA_00004";
        sink.flowActionId = "alarm";
        sink.fps          = -1;
        sink.que          = std::make_shared<AlgDataQueue<AlgDataPtr>>("stranger-alarms");
        REQUIRE(action.RegistTaskQueue(sink));
    }
    ~StrangerFlow() {
        action.RemoveTaskQueue(sink);
    }
    VideoFramePtr Feed(int64_t index, int64_t timestamp, std::vector<AiDetectRstEl> targets,
                       bool complete = true, int64_t stream = 1, std::string context = "faces-v1") {
        auto data            = std::make_shared<AlgData>();
        data->channelId      = "channel";
        data->taskId         = "stranger-task";
        data->dataType       = AlgDataType::TaskDataRecognizer;
        data->bHaveTrack     = true;
        data->bHaveRelated   = true;
        data->firstTimePoint = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(timestamp);
        data->chanDataDec.frame =
            std::make_shared<media::VideoFrame>(16, 16, media::PixelFormat::PIXEL_I420, index, timestamp);
        data->chanDataDec.frame->SetStreamIndex(stream);
        REQUIRE(data->chanDataDec.frame->Active());
        auto result                  = std::make_shared<DataDetTrackClassify>();
        result->dataType             = data->dataType;
        result->streamIndex          = stream;
        result->frameIndex           = index;
        result->timestamp            = timestamp;
        result->bHaveArea            = true;
        result->recognition_context  = std::move(context);
        result->observation_complete = complete;
        result->targets              = std::move(targets);
        data->SetTaskResult(data->dataType, result);
        action.HandFrame(data);
        return data->chanDataDec.frame;
    }
};
AiDetectRstEl Observation(int id, FaceObservationStatus status = FaceObservationStatus::kUnmatched,
                          float quality = 80) {
    AiDetectRstEl target;
    target.trackId           = id;
    target.trackIdInfo       = "track-" + std::to_string(id);
    target.relatedEl.bActive = true;
    target.box               = {0, 0, 16, 16};
    target.relatedEl.box     = {2, 1, 8, 8};
    target.confidence.label  = "pedestrian";
    target.face_observation  = {status, quality};
    TargetAreaUnit area;
    area.area_id   = "area";
    area.area_name = "Area";
    target.areaSign.areas.push_back(area);
    return target;
}
}  // namespace

TEST_CASE("Stranger flow emits each departed track with its own best evidence frame", "[stranger][flow]") {
    StrangerFlow flow;
    auto first  = flow.Feed(1, 0, {Observation(1, FaceObservationStatus::kUnmatched, 90), Observation(2)});
    auto second = flow.Feed(2, 1500, {Observation(1), Observation(2, FaceObservationStatus::kUnmatched, 95)});
    flow.Feed(3, 3000, {Observation(1), Observation(2)});
    REQUIRE(flow.sink.que->RestSize() == 0);
    flow.Feed(4, 3999, {});
    REQUIRE(flow.sink.que->RestSize() == 0);
    flow.Feed(5, 4000, {});
    REQUIRE(flow.sink.que->RestSize() == 2);
    for (const auto& expected : {std::make_pair(1, first), std::make_pair(2, second)}) {
        auto report = flow.sink.que->Pop();
        REQUIRE(report->chanDataDec.frame == expected.second);
        REQUIRE(report->chanDataDec.reportTimeStamp == 4000);
        REQUIRE(report->taskDataAlarm.alarmData->alarms.size() == 1);
        const auto& alarm = report->taskDataAlarm.alarmData->alarms.front();
        REQUIRE(alarm.trackId == expected.first);
        REQUIRE(alarm.strTrackId == "track-" + std::to_string(expected.first));
        REQUIRE(alarm.haveRelated);
        REQUIRE(alarm.relatedBox.width == 8);
    }
    flow.Feed(6, 5000, {});
    REQUIRE(flow.sink.que->RestSize() == 0);
}

TEST_CASE("A known person remains saved while later faces fail or disappear", "[stranger][flow]") {
    StrangerFlow flow;
    flow.Feed(1, 0, {Observation(1)});
    flow.Feed(2, 1000, {Observation(1, FaceObservationStatus::kMatched)});
    flow.Feed(3, 2000, {Observation(1)});
    flow.Feed(4, 3000, {Observation(1)});
    flow.Feed(5, 4000, {Observation(1, FaceObservationStatus::kNoFace)});
    flow.Feed(6, 5000, {});
    REQUIRE(flow.sink.que->RestSize() == 0);
}

TEST_CASE("Missing low quality and unavailable observations cannot make a stranger", "[stranger][flow]") {
    for (auto status : {FaceObservationStatus::kNoFace, FaceObservationStatus::kLowQuality,
                        FaceObservationStatus::kFiltered, FaceObservationStatus::kUnavailable}) {
        StrangerFlow flow;
        flow.Feed(1, 0, {Observation(1)});
        flow.Feed(2, 1500, {Observation(1, status)});
        flow.Feed(3, 3000, {Observation(1, status)});
        flow.Feed(4, 4000, {});
        REQUIRE(flow.sink.que->RestSize() == 0);
    }
}

TEST_CASE("Stranger flow cannot conclude absence from failed or stale input", "[stranger][flow]") {
    StrangerFlow flow;
    flow.Feed(1, 0, {Observation(1)});
    flow.Feed(2, 1500, {Observation(1)});
    flow.Feed(3, 3000, {Observation(1)});
    SECTION("same frame index") {
        flow.Feed(3, 5000, {});
        REQUIRE(flow.sink.que->RestSize() == 0);
    }
    SECTION("failed inference") {
        flow.Feed(4, 4000, {}, false);
        flow.Feed(5, 5000, {});
        REQUIRE(flow.sink.que->RestSize() == 0);
    }
    SECTION("long interrupted stream") {
        flow.Feed(4, 20000, {});
        REQUIRE(flow.sink.que->RestSize() == 0);
    }
    SECTION("short dropout followed by successful recognition") {
        flow.Feed(4, 3500, {});
        flow.Feed(5, 3800, {Observation(1, FaceObservationStatus::kMatched)});
        flow.Feed(6, 4800, {});
        REQUIRE(flow.sink.que->RestSize() == 0);
    }
}

TEST_CASE("Stream recognition context and action restarts discard unfinished evidence", "[stranger][flow]") {
    StrangerFlow flow;
    flow.Feed(1, 0, {Observation(1)});
    flow.Feed(2, 1500, {Observation(1)});
    flow.Feed(3, 3000, {Observation(1)});
    SECTION("stream restart") {
        flow.Feed(1, 4000, {}, true, 2);
    }
    SECTION("face selection changed") {
        flow.Feed(4, 4000, {}, true, 1, "faces-v2");
    }
    SECTION("action reset") {
        flow.action.ResetStateOnRestart();
        flow.Feed(4, 4000, {});
    }
    REQUIRE(flow.sink.que->RestSize() == 0);
}

TEST_CASE("Reused numeric track IDs do not inherit another UUID's known identity", "[stranger][flow]") {
    StrangerFlow flow;
    flow.Feed(1, 0, {Observation(1, FaceObservationStatus::kMatched)});
    auto stranger        = Observation(1);
    stranger.trackIdInfo = "new-track-1";
    flow.Feed(2, 1000, {stranger});
    flow.Feed(3, 2500, {stranger});
    flow.Feed(4, 4000, {stranger});
    flow.Feed(5, 5000, {});
    REQUIRE(flow.sink.que->RestSize() == 1);
    REQUIRE(flow.sink.que->Pop()->taskDataAlarm.alarmData->alarms.front().strTrackId == "new-track-1");
}
