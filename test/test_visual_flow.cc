#include <atomic>
#include <condition_variable>
#include <thread>

#include "catch_amalgamated.hpp"
#include "flow/common/VisualJudgment.h"
#include "service/ai/impl/VisualDecisionProtocol.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include "flow/qwen3vl/Qwen3VLWorker.h"
#include "media/IOsdTextRenderer.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "service/ai/ILlmInferService.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#endif

namespace {
using namespace cosmo;
using namespace cosmo::service;
using Json = nlohmann::json;
using namespace std::chrono_literals;
MsgDynamicKeyValue Parameter(const std::string& key, const std::string& value) {
    MsgDynamicKeyValue result;
    result.key   = key;
    result.value = value;
    result.keys  = {key};
    return result;
}
class Questions final : public IVisualQuestionService {
public:
    std::atomic<int> count{0};
    std::vector<VisualQuestionSpec> latest;
    std::shared_future<VisualQuestionPreparation> Prepare(std::vector<VisualQuestionSpec> specs,
                                                          std::shared_ptr<VisualDecisionRun>,
                                                          std::chrono::milliseconds) override {
        ++count;
        latest = specs;
        VisualQuestionPreparation result;
        result.ready          = true;
        result.manifestSha256 = std::string(64, 'a');
        for (const auto& s : specs)
            result.questions.push_back({s.itemId,
                                        s.question.at("id"),
                                        s.question.at("version"),
                                        std::string(64, 'b'),
                                        2,
                                        {"false", "true"},
                                        {{"value", 1.0}, {"bucket", "test"}, {"source", "test"}}});
        std::promise<VisualQuestionPreparation> promise;
        promise.set_value(result);
        return promise.get_future().share();
    }
};
class Decisions final : public IVisualDecisionService {
public:
    std::atomic<int> count{0};
    std::atomic<bool> block{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<VisualDecisionRequest> requests;
    bool Available() const override {
        return true;
    }
    Json Counters() const override {
        return {};
    }
    VisualDecisionResult Decide(const VisualDecisionRequest& request, std::shared_ptr<VisualDecisionRun> run,
                                Prepare prepare, std::chrono::milliseconds) override {
        auto image = prepare();
        {
            std::lock_guard<std::mutex> lock(mutex);
            requests.push_back(request);
            ++count;
            changed.notify_all();
        }
        auto until = std::chrono::steady_clock::now() + 3s;
        while (block && run->Active() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(2ms);
        Json identity = {{"request_id", util::GenerateUUID()},
                         {"frame_id", request.frameId},
                         {"roi_id", request.roiId},
                         {"task_id", run->taskId},
                         {"run_epoch", run->runEpoch},
                         {"config_revision", run->configRevision},
                         {"manifest_sha256", std::string(64, 'a')},
                         {"items", Json::array()}};
        for (const auto& q : request.questions)
            identity["items"].push_back(visual::ItemIdentity(q));
        auto result = visual::Failure(identity, image.jpeg.empty() ? "empty_or_invalid_roi" : "");
        if (!image.jpeg.empty()) {
            result["status"] = "completed";
            for (auto& item : result["items"]) {
                item["status"]        = "completed";
                item["top1"]          = "false";
                item["probabilities"] = {0.8, 0.2};
            }
        }
        return {identity, result};
    }
    bool Wait(int wanted) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, 3s, [&] { return count >= wanted; });
    }
};
MsgTaskArea Region(const std::string& id, double x, double width,
                   std::vector<MsgDynamicKeyValue> params = {}) {
    MsgTaskArea area;
    area.areaId   = id;
    area.pointBox = {x, 0, width, 1};
    area.points   = {{x, 0}, {x + width, 0}, {x + width, 1}, {x, 1}};
    area.params   = std::move(params);
    return area;
}
auto Image() {
    return VisualDecisionImage{{0xff, 0xd8, 0xff, 0xd9}, 32, 64};
}
}  // namespace

TEST_CASE("Visual task plan binds ROI overrides and preserves question order through DTO round trips",
          "[visual-flow]") {
    Questions questions;
    Decisions decisions;
    test::ScopedServiceOverride<IVisualQuestionService> q(questions);
    test::ScopedServiceOverride<IVisualDecisionService> d(decisions);
    Json one = {{"id", "one"}, {"version", 1}, {"type", "noul"}, {"instructions", "火焰?"}};
    Json two = {{"id", "two"}, {"version", 2}, {"type", "noul"}, {"instructions", "安全帽?"}};
    std::vector<MsgDynamicKeyValue> parameters{Parameter("visual.question.one", one.dump()),
                                               Parameter("visual.question.two", two.dump()),
                                               Parameter("visual.questions", "[\"one\"]")};
    parameters = Json(parameters).get<std::vector<MsgDynamicKeyValue>>();
    VisualParameters values;
    UpdateVisualParameters(values, parameters);
    auto area = Region("door", 0, 0.5, {Parameter("visual.questions", "[\"two\",\"one\"]")});
    area      = Json(area).get<MsgTaskArea>();
    VisualJudgment task("task", "default", false, values, {area});
    auto first  = task.Decide("frame", "roi-1", "door", Image);
    auto second = task.Decide("frame", "roi-2", "", Image);
    REQUIRE(first.response.at("items").size() == 2);
    CHECK(first.response["items"][0]["question_id"] == "two");
    CHECK(first.response["items"][1]["question_id"] == "one");
    REQUIRE(second.response.at("items").size() == 1);
    CHECK(second.response["items"][0]["question_id"] == "one");
    CHECK(questions.count == 1);  // Preparation is never repeated per ROI/frame.
    CHECK(decisions.count == 2);
    auto originalRevision = task.Run()->configRevision;
    area.points[0].x += 0.1;
    VisualJudgment moved("task", "default", false, values, {area});
    CHECK(moved.Run()->configRevision != originalRevision);
    task.Invalidate();
    CHECK(task.Decide("frame", "roi", "door", Image).response["reason"] == "stale_task_run");
    CHECK(decisions.count == 2);
}

TEST_CASE("Visual configuration rejects invalid bindings and unqualified filtering without inference",
          "[visual-flow]") {
    Questions questions;
    Decisions decisions;
    test::ScopedServiceOverride<IVisualQuestionService> q(questions);
    test::ScopedServiceOverride<IVisualDecisionService> d(decisions);
    auto bad = GENERATE(0, 1, 2, 3);
    VisualParameters values;
    if (bad == 0)
        values["visual.questions"] = "[\"missing\"]";
    if (bad == 1)
        values["visual.questions"] = "[]";
    if (bad == 2)
        values["visual.mode"] = "filter";
    if (bad == 3)
        values["visual.question.broken"] = "{\"id\":\"broken\"}";
    VisualJudgment task("task", "helmet", false, values, {});
    auto result = task.Decide("frame", "roi", "", Image);
    CHECK(result.response.at("status") == "unknown");
    CHECK(decisions.count == 0);
    CHECK(questions.count == 0);
}

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
namespace {
class Device final : public mem::IDeviceContext {
public:
    void* GetMemoryHandle() override {
        return nullptr;
    }
    void* GetMediaHandle() override {
        return nullptr;
    }
};
class Text final : public media::IOsdTextRenderer {
public:
    bool Init(const std::string&) override {
        return true;
    }
    bool IsReady() const override {
        return false;
    }
    TextBitmap RenderString(const std::string&, float) const override {
        return {};
    }
    OutlinedTextBitmap RenderStringWithOutline(const std::string&, float) const override {
        return {};
    }
};
class Frames final : public VideoFrameServiceImpl {
public:
    int failure{0};
    VideoFramePtr CopyJpegSrcFrame(VideoFramePtr frame) override {
        if (failure == 1)
            return nullptr;
        if (failure == 2)
            throw std::runtime_error("copy failure");
        return frame;
    }
    VideoFramePtr Crop(VideoFramePtr frame, const util::Box box) override {
        if (failure == 3)
            throw std::runtime_error("crop failure");
        return VideoFrameServiceImpl::Crop(frame, box);
    }
    std::vector<u_char> EncodeJpeg(VideoFramePtr frame) override {
        if (failure == 4)
            return {};
        return VideoFrameValid(frame) ? std::vector<u_char>{0xff, 0xd8, 0xff, 0xd9} : std::vector<u_char>{};
    }
};
class ForbiddenLlm final : public ILlmInferService {
public:
    std::atomic<int> touched{0};
    bool EnsureInit(const std::string&) override {
        ++touched;
        return false;
    }
    bool IsInitialized() const override {
        return false;
    }
    util::ErrorEnum Generate(const std::vector<VideoFramePtr>&, const std::vector<std::string>&,
                             const Qwen3VLGenerationParam&, std::vector<Qwen3VLResult>&) override {
        ++touched;
        return util::ErrorEnum::Failed;
    }
    util::ErrorEnum GetMaxBatchSize(size_t&) const override {
        return util::ErrorEnum::Failed;
    }
    void Reset() override {
        ++touched;
    }
    void NotifyWorkerStart() override {
        ++touched;
    }
    void NotifyWorkerStop() override {
        ++touched;
    }
};
struct FlowFixture {
    Device device;
    Text text;
    test::ScopedServiceOverride<mem::IDeviceContext> deviceRegistration{device};
    test::ScopedServiceOverride<media::IOsdTextRenderer> textRegistration{text};
    mem::MemoryPoolMng pool{std::make_unique<mem::AllocatorCpu>(), {64 * 64 * 3}};
    struct PoolScope {
        explicit PoolScope(mem::MemoryPoolMng& value) {
            mem::SetMemoryPoolContext(&value);
        }
        ~PoolScope() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } poolScope{pool};
    Frames frames;
    Questions questions;
    Decisions decisions;
    ForbiddenLlm llm;
    test::ScopedServiceOverride<IVideoFrameTransform> transform{frames};
    test::ScopedServiceOverride<IVideoFrameOSD> osd{frames};
    test::ScopedServiceOverride<IVideoFrameCodec> codec{frames};
    test::ScopedServiceOverride<IVisualQuestionService> questionRegistration{questions};
    test::ScopedServiceOverride<IVisualDecisionService> decisionRegistration{decisions};
    test::ScopedServiceOverride<ILlmInferService> llmRegistration{llm};
    ActionNode action;
    Qwen3VLWorker worker{action};
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> output = std::make_shared<AlgDataQueue<AlgDataPtr>>("result");
    FlowFixture() {
        worker.AddTask("camera", "task");
        auto params = std::vector<MsgDynamicKeyValue>{Parameter("vlmProvider", "laya_v"),
                                                      Parameter("keywords", "helmet")};
        worker.SetParam("camera", "task", params);
        AlgTaskUnit downstream;
        downstream.channel_id = "camera";
        downstream.task_id    = "task";
        downstream.que        = output;
        downstream.fps        = -1;
        downstream.actionId   = "alarm";
        worker.RegistTaskQueue(downstream);
    }
    AlgDataPtr Frame(int64_t index = 1) {
        auto data               = std::make_shared<AlgData>();
        data->channelId         = "camera";
        data->taskId            = "task";
        data->firstTimePoint    = std::chrono::steady_clock::now();
        data->chanDataDec.frame = std::make_shared<media::VideoFrame>(64, 64, media::PixelFormat::PIXEL_BGR8);
        data->chanDataDec.frame->SetFrameIndex(index);
        return data;
    }
    AlgDataPtr Receive() {
        if (!output->WaitForData(3000))
            return nullptr;
        return output->Pop();
    }
};
}  // namespace

TEST_CASE("Laya video path binds independent ROI prompts and keeps negative results in review mode",
          "[visual-flow][video]") {
    FlowFixture f;
    std::vector<MsgTaskArea> regions{Region("left", 0, 0.5),
                                     Region("right", 0.5, 0.5, {Parameter("keywords", "smoke")})},
        shield;
    REQUIRE(f.worker.SetArea("camera", "task", regions, shield));
    const int prepared = f.questions.count;
    REQUIRE(f.worker.Start());
    REQUIRE(f.worker.GetQueue()->Insert(f.Frame()));
    auto result = f.Receive();
    REQUIRE(result);
    REQUIRE(result->taskDataAlarm.alarmData);
    const auto& alarms = result->taskDataAlarm.alarmData->alarms;
    REQUIRE(alarms.size() == 2);
    CHECK(alarms[0].areaId == "left");
    CHECK(alarms[1].areaId == "right");
    for (const auto& alarm : alarms) {
        REQUIRE(alarm.visualJudgments.size() == 1);
        const auto& record = alarm.visualJudgments[0];
        CHECK(record["result"]["items"][0]["top1"] == "false");
        CHECK(record["alarm_filter_applied"] == false);
        CHECK(record["business_qualified"] == false);
    }
    CHECK(alarms[0].visualJudgments[0]["request"]["items"][0]["question_id"] !=
          alarms[1].visualJudgments[0]["request"]["items"][0]["question_id"]);
    CHECK(f.questions.count == prepared);
    f.worker.Stop();
    CHECK(f.llm.touched == 0);
    CHECK_FALSE(alarms[0].visualRun->Active());
}

TEST_CASE("Laya video edit fences an in-flight result and restart prepares a fresh run",
          "[visual-flow][video]") {
    FlowFixture f;
    f.decisions.block = true;
    REQUIRE(f.worker.Start());
    REQUIRE(f.worker.GetQueue()->Insert(f.Frame()));
    REQUIRE(f.decisions.Wait(1));
    auto change = std::vector<MsgDynamicKeyValue>{Parameter("keywords", "fire")};
    REQUIRE(f.worker.ModifyParam("camera", "task", change));
    f.decisions.block = false;
    REQUIRE_FALSE(f.output->WaitForData(100));
    REQUIRE(f.worker.GetQueue()->Insert(f.Frame(2)));
    auto result = f.Receive();
    REQUIRE(result);
    REQUIRE(result->taskDataAlarm.alarmData->alarms.size() == 1);
    auto firstRun = result->taskDataAlarm.alarmData->alarms[0].visualRun;
    f.worker.Stop();
    REQUIRE_FALSE(firstRun->Active());
    REQUIRE(f.worker.Start());
    REQUIRE(f.worker.GetQueue()->Insert(f.Frame(3)));
    result = f.Receive();
    REQUIRE(result);
    REQUIRE(result->taskDataAlarm.alarmData->alarms.size() == 1);
    CHECK(result->taskDataAlarm.alarmData->alarms[0].visualRun->runEpoch != firstRun->runEpoch);
    f.worker.Stop();
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya target ROIs use each overlapping region and retain original target association",
          "[visual-flow][video][roi]") {
    FlowFixture f;
    std::vector<MsgTaskArea> regions{Region("left", 0, 0.5),
                                     Region("overlap", 0.25, 0.5, {Parameter("keywords", "smoke")})},
        shield;
    REQUIRE(f.worker.SetArea("camera", "task", regions, shield));
    auto data                   = f.Frame();
    data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    AiDetectRstEl target;
    target.box         = {16, 8, 16, 24};
    target.targetId    = "original-target";
    target.trackIdInfo = "original-track";
    target.trackId     = 42;
    data->chanDataDetect.detRet->targets.push_back(target);
    target.box      = {52, 8, 8, 24};
    target.targetId = "outside-target";
    data->chanDataDetect.detRet->targets.push_back(target);
    REQUIRE(f.worker.Start());
    REQUIRE(f.worker.GetQueue()->Insert(data));
    auto result = f.Receive();
    REQUIRE(result);
    const auto& alarms = result->taskDataAlarm.alarmData->alarms;
    REQUIRE(alarms.size() == 3);
    CHECK(alarms[0].areaId == "left");
    CHECK(alarms[1].areaId == "overlap");
    CHECK(alarms[2].areaId.empty());
    const auto& first  = alarms[0].visualJudgments[0];
    const auto& second = alarms[1].visualJudgments[0];
    CHECK(first["source_target_id"] == "original-target");
    CHECK(second["source_track_id"] == "original-track");
    CHECK(second["source_track_index"] == 42);
    CHECK(first["request"]["roi_id"] != second["request"]["roi_id"]);
    CHECK(first["request"]["items"][0]["question_id"] != second["request"]["items"][0]["question_id"]);
    CHECK(alarms[2].visualJudgments[0]["source_target_id"] == "outside-target");
    CHECK(alarms[2].visualJudgments[0]["request"]["items"][0]["question_id"] == "legacy-default");
    f.worker.Stop();
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya ROI image failures remain explicit unknowns and never use full-frame fallback",
          "[visual-flow][video][roi]") {
    FlowFixture f;
    f.frames.failure = GENERATE(1, 2, 3, 4);
    std::vector<MsgTaskArea> regions{Region("left", 0, 0.5)}, shield;
    REQUIRE(f.worker.SetArea("camera", "task", regions, shield));
    REQUIRE(f.worker.Start());
    REQUIRE(f.worker.GetQueue()->Insert(f.Frame()));
    auto result = f.Receive();
    REQUIRE(result);
    REQUIRE(result->taskDataAlarm.alarmData->alarms.size() == 1);
    const auto& unit = result->taskDataAlarm.alarmData->alarms.front();
    CHECK(unit.areaId == "left");
    CHECK(unit.visualJudgments[0]["request"]["roi_id"] == "area-0");
    CHECK(unit.visualJudgments[0]["result"]["status"] == "unknown");
    CHECK(unit.visualJudgments[0]["result"]["reason"] == "empty_or_invalid_roi");
    CHECK(unit.box.width == 32);
    f.worker.Stop();
    CHECK(f.llm.touched == 0);
}
#endif
