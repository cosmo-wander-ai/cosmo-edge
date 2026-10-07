#include <atomic>
#include <condition_variable>
#include <thread>

#include "catch_amalgamated.hpp"
#include "flow/alarm/AlarmReviewRoi.h"
#include "flow/alarm/AlarmVisualPlan.h"
#include "flow/common/VisualJudgment.h"
#include "service/ai/impl/VisualDecisionProtocol.h"
#include "support/ScopedServiceOverride.h"
#include "util/UuidUtil.h"

#if defined(COSMO_MEDIA_USE_CPU_BACKEND)
#include "api/MessageHandler.h"
#include "flow/qwen3vl/PQwen3VLWorker.h"
#include "flow/qwen3vl/Qwen3VLWorker.h"
#include "flow/task/PTaskBase.h"
#include "media/IOsdTextRenderer.h"
#include "mem/AllocatorCpu.h"
#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#include "mock/MockAppInfoService.h"
#include "service/ai/ILlmInferService.h"
#include "service/media/impl/PicTaskServiceImpl.h"
#include "service/media/impl/VideoFrameServiceImpl.h"
#include "util/dto/ActionCodes.h"
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
    bool applyPolicy{false};
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
        if (applyPolicy && request.mode == "filter") {
            const bool retain  = image.jpeg.empty() || request.questions.at(0).questionId == "right";
            result["decision"] = {{"mode", "filter"},
                                  {"business_qualified", true},
                                  {"retain", retain},
                                  {"filter_applied", !retain},
                                  {"verdict", retain ? "accept" : "reject"}};
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
    if (GENERATE(false, true))
        parameters = {
            Parameter("visual.catalog", Json{{"questions", {one, two}}, {"default", {"one"}}}.dump())};
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

TEST_CASE("Alarm visual configuration preserves legacy subject priority and prepares each model label once",
          "[visual-flow][alarm-plan]") {
    Questions questions;
    Decisions decisions;
    test::ScopedServiceOverride<IVisualQuestionService> q(questions);
    test::ScopedServiceOverride<IVisualDecisionService> d(decisions);
    std::vector<std::string> labels;
    for (int i = 0; i < 80; ++i)
        labels.push_back("class-" + std::to_string(i));
    AlarmVisualPlan plan("task", "", "fire detection", labels, {}, {});
    REQUIRE(questions.latest.size() == 81);
    const int prepared = questions.count;
    DataAlarmUnit unit;
    unit.confidence.push_back({"class-79", "", 0.9F});
    unit.attrRsts.push_back({"", "class-2", "", 0.8F});
    CHECK(plan.Subject(unit).source == "confidence_label");
    auto confidence = plan.Decide(unit, "frame", "roi", Image);
    REQUIRE(confidence.AllCompleted());
    const auto confidenceId = confidence.request["items"][0]["question_id"];
    unit.confidence.clear();
    CHECK(plan.Subject(unit).source == "attribute_label");
    auto attribute = plan.Decide(unit, "frame", "roi", Image);
    REQUIRE(attribute.AllCompleted());
    CHECK(attribute.request["items"][0]["question_id"] != confidenceId);
    unit.attrRsts.clear();
    CHECK(plan.Subject(unit).source == "algorithm_name");
    CHECK(plan.Decide(unit, "frame", "roi", Image).request["items"][0]["question_id"] == "legacy-default");
    CHECK(questions.count == prepared);
    AlarmVisualPlan custom("task", "custom behavior", "name", labels, {}, {});
    CHECK(questions.latest.size() == 1);
    unit.confidence.push_back({"not in catalog", "", 0.9F});
    CHECK(custom.Subject(unit).source == "custom");
    CHECK(custom.Decide(unit, "frame", "roi", Image).AllCompleted());
    CHECK(SelectAlarmReviewSubject({}, "", "").source == "generic");
    CHECK(AlarmReviewInstruction(SelectAlarmReviewSubject({}, "", "")) ==
          "告警审核。图片为按告警框裁剪后的目标图。判断图片中是否存在有效目标或对应行为。");
}

TEST_CASE(
    "Unknown alarm labels never use a different fallback question and explicit ROI questions take precedence",
    "[visual-flow][alarm-plan]") {
    Questions questions;
    Decisions decisions;
    test::ScopedServiceOverride<IVisualQuestionService> q(questions);
    test::ScopedServiceOverride<IVisualDecisionService> d(decisions);
    auto area = Region("door", 0, 0.5, {Parameter("keywords", "door behavior")});
    AlarmVisualPlan plan("task", "", "name", {"known"}, {}, {area});
    DataAlarmUnit unit;
    unit.confidence.push_back({"unknown", "", 0.9F});
    auto result = plan.Decide(unit, "frame", "roi", Image);
    CHECK(result.response["reason"] == "unprepared_semantic_label");
    CHECK(decisions.count == 0);
    unit.areaId = "door";
    REQUIRE(plan.Decide(unit, "frame", "roi", Image).AllCompleted());
    CHECK(decisions.count == 1);
    Json definition = {
        {"id", "explicit"}, {"version", 1}, {"type", "noul"}, {"instructions", "a custom task question"}};
    VisualParameters config{{"visual.question.explicit", definition.dump()},
                            {"visual.questions", "[\"explicit\"]"}};
    if (GENERATE(false, true))
        config = {{"visual.catalog", Json{{"questions", {definition}}, {"default", {"explicit"}}}.dump()}};
    AlarmVisualPlan explicitPlan("task", "", "name", {}, config, {});
    REQUIRE(explicitPlan.Decide(unit, "frame", "roi", Image).AllCompleted());
    CHECK(decisions.requests.back().questions[0].questionId == "explicit");
    plan.Invalidate();
    CHECK(plan.Decide(unit, "frame", "roi", Image).response["reason"] == "stale_task_run");
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
    bool EnsureHostData(VideoFramePtr frame) override {
        return failure != 5 && VideoFrameServiceImpl::EnsureHostData(frame);
    }
    VideoFramePtr DecodeJpeg(const std::vector<u_char>&) override {
        return std::make_shared<media::VideoFrame>(64, 64, media::PixelFormat::PIXEL_BGR8);
    }
    VideoFramePtr CopyJpegSrcFrame(VideoFramePtr frame) override {
        if (failure == 1)
            return nullptr;
        if (failure == 2)
            throw std::runtime_error("copy failure");
        if (failure == 8)
            return std::make_shared<media::VideoFrame>(32, 32, media::PixelFormat::PIXEL_BGR8);
        return frame;
    }
    VideoFramePtr Crop(VideoFramePtr frame, const util::Box box) override {
        if (failure == 3)
            throw std::runtime_error("crop failure");
        if (failure == 6)
            return nullptr;
        if (failure == 7)
            return frame;
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
    mem::MemoryPoolMng pool{std::make_unique<mem::AllocatorCpu>(), {64 * 64 * 3, 256 * 192 * 3}};
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

TEST_CASE("Typed alarm ROI preserves actual crop and rejects every full-frame substitution",
          "[visual-flow][alarm-roi]") {
    FlowFixture fixture;
    auto frame = std::make_shared<media::VideoFrame>(256, 192, media::PixelFormat::PIXEL_BGR8);
    REQUIRE(VideoFrameValid(frame));
    frame->SetFrameIndex(123);
    frame->SetTimestamp(456);
    frame->SetStreamIndex(7);
    const util::Box target(100, 80, 20, 30);
    SECTION("measured crop retains source geometry and frame identity") {
        auto roi = PrepareAlarmReviewRoiStrict(frame, target);
        INFO(roi.failure);
        REQUIRE(VideoFrameValid(roi.frame));
        CHECK(roi.failure.empty());
        CHECK(roi.mode == "cropped");
        CHECK(roi.requested == util::Box(52, 32, 116, 126));
        CHECK(roi.actual == roi.requested);
        CHECK(roi.sourceWidth == 256);
        CHECK(roi.sourceHeight == 192);
        CHECK(roi.frame->GetWidth() == 116);
        CHECK(roi.frame->GetHeight() == 126);
        CHECK(roi.frame->GetFrameIndex() == 123);
        CHECK(roi.frame->GetTimestamp() == 456);
        CHECK(roi.frame->GetStreamIndex() == 7);
    }
    SECTION("a requested full image is explicit and still host verified") {
        auto roi = PrepareAlarmReviewRoiStrict(frame, {0, 0, 256, 192});
        REQUIRE(VideoFrameValid(roi.frame));
        CHECK(roi.mode == "full_frame");
        CHECK(roi.actual == util::Box(0, 0, 256, 192));
    }
    SECTION("failed preparation retains a stage and never returns fallback pixels") {
        const auto fault       = GENERATE(1, 2, 3, 5, 6, 7, 8);
        fixture.frames.failure = fault;
        const std::map<int, std::string> reasons{
            {1, "source_copy_failed"},        {2, "source_copy_failed"}, {3, "roi_crop_failed"},
            {5, "roi_host_data_unavailable"}, {6, "roi_crop_failed"},    {7, "roi_bounds_unverified"},
            {8, "source_geometry_mismatch"}};
        auto roi = PrepareAlarmReviewRoiStrict(frame, target);
        CHECK_FALSE(VideoFrameValid(roi.frame));
        CHECK(roi.failure == reasons.at(fault));
    }
    SECTION("invalid source is a typed failure") {
        auto roi = PrepareAlarmReviewRoiStrict({}, target);
        CHECK_FALSE(roi.frame);
        CHECK(roi.failure == "invalid_source_frame");
    }
    SECTION("padding cannot rescue empty or off-image targets") {
        for (const auto& box : std::vector<util::Box>{{100, 80, 0, 30},
                                                      {100, 80, -1, 30},
                                                      {-25, 80, 20, 30},
                                                      {257, 80, 20, 30},
                                                      {100, -40, 20, 30},
                                                      {100, 193, 20, 30}}) {
            auto roi = PrepareAlarmReviewRoiStrict(frame, box);
            CHECK_FALSE(roi.frame);
            CHECK(roi.failure == "invalid_target_roi");
        }
    }
    SECTION("legacy crop-failure policy remains isolated") {
        fixture.frames.failure = 6;
        auto roi               = PrepareAlarmReviewRoi(frame, target);
        REQUIRE(VideoFrameValid(roi.frame));
        CHECK(roi.mode == "full_frame_fallback");
        CHECK(roi.actual == util::Box(0, 0, 256, 192));
    }
}

TEST_CASE("Laya video path binds independent ROI prompts and keeps negative results in review mode",
          "[visual-flow][video]") {
    FlowFixture f;
    SECTION("only the Laya task is configured") {}
    SECTION("a stopped local VLM task shares the worker") {
        REQUIRE(f.worker.AddTask("idle-camera", "idle-task"));
        auto params = std::vector<MsgDynamicKeyValue>{Parameter("vlmProvider", "local_model")};
        REQUIRE(f.worker.SetParam("idle-camera", "idle-task", params));
    }
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

TEST_CASE("Laya video filtering emits only retained ROI alarms", "[visual-flow][video][visual-policy]") {
    FlowFixture f;
    f.decisions.applyPolicy = true;
    Json catalog{{"questions",
                  {{{"id", "left"}, {"version", 1}, {"type", "noul"}, {"instructions", "fire?"}},
                   {{"id", "right"}, {"version", 1}, {"type", "noul"}, {"instructions", "smoke?"}}}},
                 {"default", {"left"}},
                 {"decision", {{"mode", "filter"}, {"profile_id", "fixture"}}}};
    std::vector<MsgDynamicKeyValue> params{Parameter("visual.catalog", catalog.dump())};
    REQUIRE(f.worker.ModifyParam("camera", "task", params));
    std::vector<MsgTaskArea> regions{Region("left", 0, .5),
                                     Region("right", .5, .5, {Parameter("visual.questions", "[\"right\"]")})},
        shield;
    REQUIRE(f.worker.SetArea("camera", "task", regions, shield));
    REQUIRE(f.worker.Start());
    REQUIRE(f.worker.GetQueue()->Insert(f.Frame()));
    auto result = f.Receive();
    REQUIRE(result);
    const auto& alarms = result->taskDataAlarm.alarmData->alarms;
    REQUIRE(alarms.size() == 1);
    CHECK(alarms[0].areaId == "right");
    CHECK(f.decisions.count == 2);
    f.worker.Stop();
    CHECK(f.llm.touched == 0);
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
    f.frames.failure = GENERATE(1, 2, 3, 4, 5);
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

namespace {
ActionNode PictureVisualAction(const std::string& id = "judge") {
    ActionNode action;
    action.actionId            = PDAQwen3VL_Code;
    action.flowActionId        = id;
    action.configObject.params = {Parameter("vlmProvider", "laya_v"), Parameter("keywords", "helmet")};
    return action;
}
MsgPTaskDetectPicRecv PictureRequest() {
    MsgPTaskDetectPicRecv request;
    request.imageData  = {0xff, 0xd8, 0xff, 0xd9};
    request.needRetImg = false;
    return request;
}
}  // namespace

TEST_CASE("Laya picture pipeline publishes per-node ROI questions without invented detection targets",
          "[visual-flow][picture]") {
    FlowFixture f;
    PTaskBase base;
    auto alg      = std::make_shared<ActionAlg>();
    alg->workFlow = {PictureVisualAction("one"), PictureVisualAction("two")};
    auto task     = base.TaskCreate("picture", alg);
    REQUIRE(task);
    MsgTaskConfig config;
    config.areas = {Region("left", 0, 0.5), Region("right", 0.5, 0.5, {Parameter("keywords", "smoke")})};
    task->params = config;
    REQUIRE(base.ModifyTaskParam(task, config));
    REQUIRE(base.TaskActionInit(task));
    auto request = PictureRequest();
    MsgPTaskDetectPicSend response;
    const int prepared = f.questions.count;
    REQUIRE(base.TaskDetectPic(task, request, response) == util::ErrorEnum::Success);
    REQUIRE(response.resData.visualJudgments.size() == 4);
    CHECK(response.resData.targetList.empty());
    CHECK(response.resData.areaList.empty());
    const auto& records = response.resData.visualJudgments;
    CHECK(records[0]["flow_action_id"] == "one");
    CHECK(records[2]["flow_action_id"] == "two");
    CHECK(records[0]["request"]["frame_id"] == records[2]["request"]["frame_id"]);
    CHECK(records[0]["area_id"] == "left");
    CHECK(records[1]["area_id"] == "right");
    CHECK(records[0]["request"]["items"][0]["question_id"] !=
          records[1]["request"]["items"][0]["question_id"]);
    CHECK(records[0]["result"]["items"][0]["top1"] == "false");
    CHECK(records[0]["business_qualified"] == false);
    CHECK(records[0]["alarm_filter_applied"] == false);
    CHECK(Json(response).get<MsgPTaskDetectPicSend>().resData.visualJudgments == records);
    CHECK(f.questions.count == prepared);
    auto second = PictureRequest();
    MsgPTaskDetectPicSend secondResponse;
    REQUIRE(base.TaskDetectPic(task, second, secondResponse) == util::ErrorEnum::Success);
    CHECK(secondResponse.resData.visualJudgments[0]["request"]["frame_id"] !=
          records[0]["request"]["frame_id"]);
    REQUIRE(base.TaskActionDestroy(task));
    REQUIRE(base.TaskDelete(task));
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya picture filtering returns retained regions and all review records",
          "[visual-flow][picture][visual-policy]") {
    FlowFixture f;
    f.decisions.applyPolicy = true;
    PTaskBase base;
    auto alg      = std::make_shared<ActionAlg>();
    alg->workFlow = {PictureVisualAction()};
    auto task     = base.TaskCreate("picture", alg);
    REQUIRE(task);
    Json catalog{{"questions",
                  {{{"id", "left"}, {"version", 1}, {"type", "noul"}, {"instructions", "fire?"}},
                   {{"id", "right"}, {"version", 1}, {"type", "noul"}, {"instructions", "smoke?"}}}},
                 {"default", {"left"}},
                 {"decision", {{"mode", "filter"}, {"profile_id", "fixture"}}}};
    MsgTaskConfig config;
    config.params = {Parameter("visual.catalog", catalog.dump())};
    config.areas  = {Region("left", 0, .5),
                     Region("right", .5, .5, {Parameter("visual.questions", "[\"right\"]")})};
    task->params  = config;
    REQUIRE(base.ModifyTaskParam(task, config));
    REQUIRE(base.TaskActionInit(task));
    auto request = PictureRequest();
    MsgPTaskDetectPicSend response;
    REQUIRE(base.TaskDetectPic(task, request, response) == util::ErrorEnum::Success);
    REQUIRE(response.resData.visualJudgments.size() == 2);
    CHECK(response.resData.visualJudgments[0]["alarm_filter_applied"] == true);
    CHECK(response.resData.visualJudgments[1]["alarm_filter_applied"] == false);
    REQUIRE(response.resData.targetList.size() == 1);
    REQUIRE(base.TaskActionDestroy(task));
    REQUIRE(base.TaskDelete(task));
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya picture service replaces removed catalog overrides and preserves default provider",
          "[visual-flow][picture]") {
    FlowFixture f;
    PicTaskServiceImpl service;
    auto alg      = std::make_shared<ActionAlg>();
    alg->workFlow = {PictureVisualAction()};
    REQUIRE(service.TaskCreate("picture", alg) == util::ErrorEnum::Success);
    Json one = {{"id", "one"}, {"version", 1}, {"type", "noul"}, {"instructions", "fire?"}};
    Json two = {{"id", "two"}, {"version", 1}, {"type", "noul"}, {"instructions", "smoke?"}};
    MsgTaskConfig config;
    config.params = {Parameter("visual.question.one", one.dump()),
                     Parameter("visual.question.two", two.dump()),
                     Parameter("visual.questions", "[\"two\",\"one\"]")};
    REQUIRE(service.SetTaskParam("picture", config));
    auto request = PictureRequest();
    MsgPTaskDetectPicSend first;
    REQUIRE(service.DetectPic("picture", request, first) == util::ErrorEnum::Success);
    REQUIRE(first.resData.visualJudgments.size() == 1);
    const auto& record = first.resData.visualJudgments[0];
    REQUIRE(record["result"]["items"].size() == 2);
    CHECK(record["result"]["items"][0]["question_id"] == "two");
    CHECK(record["result"]["items"][1]["question_id"] == "one");
    config = {};
    REQUIRE(service.SetTaskParam("picture", config));
    auto next = PictureRequest();
    MsgPTaskDetectPicSend second;
    REQUIRE(service.DetectPic("picture", next, second) == util::ErrorEnum::Success);
    REQUIRE(second.resData.visualJudgments.size() == 1);
    const auto& replacement = second.resData.visualJudgments[0];
    REQUIRE(replacement["result"]["items"].size() == 1);
    CHECK(replacement["result"]["items"][0]["question_id"] == "legacy-default");
    CHECK(replacement["request"]["config_revision"] != record["request"]["config_revision"]);
    REQUIRE(service.TaskDelete("picture") == util::ErrorEnum::Success);
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya picture edit and stop fence in-flight results and restart changes epoch",
          "[visual-flow][picture]") {
    FlowFixture f;
    auto action = PictureVisualAction();
    PQwen3VLWorker worker(action, "picture");
    REQUIRE(worker.ActionInit());
    auto data         = f.Frame();
    f.decisions.block = true;
    auto pending      = std::async(std::launch::async, [&] { return worker.HandPic(data); });
    REQUIRE(f.decisions.Wait(1));
    auto change = std::vector<MsgDynamicKeyValue>{Parameter("keywords", "smoke")};
    auto mode   = GENERATE(0, 1, 2);
    if (mode == 0)
        REQUIRE(worker.ModifyParam("picture", change));
    else if (mode == 1) {
        std::vector<MsgTaskArea> regions{Region("door", 0, 0.5)}, shield;
        REQUIRE(worker.SetArea("picture", regions, shield));
    } else
        worker.ActionDestroy();
    f.decisions.block = false;
    CHECK(pending.get() == util::ErrorEnum::ActionStop);
    CHECK(data->visualDecisions.empty());
    CHECK_FALSE(worker.ModifyParam("wrong-task", change));
    REQUIRE(worker.ActionInit());
    auto next = f.Frame(2);
    REQUIRE(worker.HandPic(next) == util::ErrorEnum::Success);
    REQUIRE(next->visualDecisions.size() == 1);
    auto oldRun = next->visualDecisions[0].run;
    auto cloned = AlgDataCopy(next);
    REQUIRE(cloned->visualDecisions.size() == 1);
    CHECK(cloned->visualFrameId == next->visualFrameId);
    CHECK(cloned->visualDecisions[0].records == next->visualDecisions[0].records);
    worker.ActionDestroy();
    CHECK_FALSE(oldRun->Active());
    CHECK_FALSE(cloned->visualDecisions[0].run->Active());
    CHECK(worker.HandPic(f.Frame(3)) == util::ErrorEnum::ActionStop);
    REQUIRE(worker.ActionInit());
    auto restarted = f.Frame(4);
    REQUIRE(worker.HandPic(restarted) == util::ErrorEnum::Success);
    CHECK(restarted->visualDecisions[0].run->runEpoch != oldRun->runEpoch);
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya picture ROI failures are explicit unknowns with no full-image substitution",
          "[visual-flow][picture][roi]") {
    FlowFixture f;
    auto action = PictureVisualAction();
    PQwen3VLWorker worker(action, "picture");
    std::vector<MsgTaskArea> regions{Region("door", 0, 0.5)}, shield;
    REQUIRE(worker.SetArea("picture", regions, shield));
    f.frames.failure = GENERATE(1, 2, 3, 4, 5);
    auto data        = f.Frame();
    REQUIRE(worker.HandPic(data) == util::ErrorEnum::Success);
    REQUIRE(data->visualDecisions.size() == 1);
    REQUIRE(data->visualDecisions[0].records.size() == 1);
    const auto& record = data->visualDecisions[0].records[0];
    CHECK(record["area_id"] == "door");
    CHECK(record["request"]["roi_id"] == "area-0");
    CHECK(record["result"]["status"] == "unknown");
    CHECK(record["result"]["reason"] == "empty_or_invalid_roi");
    CHECK_FALSE(data->chanDataDetect.detRet);
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya picture detector targets retain identities and zero-target upstream never falls back",
          "[visual-flow][picture][roi]") {
    FlowFixture f;
    auto action = PictureVisualAction();
    PQwen3VLWorker worker(action, "picture", true);
    std::vector<MsgTaskArea> regions{Region("door", 0, 0.75), Region("overlap", 0, 0.5)}, shield;
    REQUIRE(worker.SetArea("picture", regions, shield));
    auto data                   = f.Frame();
    data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    auto empty                  = AlgDataCopy(data);
    AiDetectRstEl target;
    target.box         = {8, 8, 16, 16};
    target.targetId    = "original";
    target.trackIdInfo = "track";
    target.trackId     = 9;
    data->chanDataDetect.detRet->targets.push_back(target);
    REQUIRE(worker.HandPic(data) == util::ErrorEnum::Success);
    REQUIRE(data->visualDecisions.size() == 1);
    REQUIRE(data->visualDecisions[0].records.size() == 2);
    CHECK(data->visualDecisions[0].records[0]["source_target_id"] == "original");
    CHECK(data->visualDecisions[0].records[1]["source_track_id"] == "track");
    CHECK(data->chanDataDetect.detRet->targets.size() == 1);
    const int count = f.decisions.count;
    REQUIRE(worker.HandPic(empty) == util::ErrorEnum::Success);
    CHECK(f.decisions.count == count);
    REQUIRE(empty->visualDecisions.size() == 1);
    CHECK(empty->visualDecisions[0].records.empty());
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Laya grouped picture response forwards typed results with event binding and saved provider",
          "[visual-flow][picture][group]") {
    FlowFixture f;
    test::MockAppInfoService info;
    test::ScopedServiceOverride<IAppInfoService> app(info);
    ALLOW_CALL(info, GetPicTaskGroupCount()).RETURN(1);
    PicTaskServiceImpl service;
    auto alg    = std::make_shared<ActionAlg>();
    auto action = PictureVisualAction();
    action.configObject.params.clear();  // Provider is a saved task override.
    alg->workFlow = {action};
    REQUIRE(service.TaskCreate("helmet-0", alg) == util::ErrorEnum::Success);
    MsgTaskConfig config;
    config.params = {Parameter("vlmProvider", "laya_v"), Parameter("keywords", "helmet")};
    REQUIRE(service.SetTaskParam("helmet-0", config));
    MsgDetectRecv input;
    input.eventCodes = {"helmet"};
    input.imageData  = "/9j/2Q==";
    MsgPTaskDetectExtParam ext;
    ext.eventCode = "helmet";
    MsgPTaskDetectExtParamRule rule{};
    rule.DetectRegion = {{0, 0}, {0.5, 0}, {0.5, 1}, {0, 1}};
    ext.rules         = {rule};
    input.extParam    = {ext};
    std::error_condition error;
    auto response = service.ProcessDetectGroup(input, error);
    REQUIRE(response.data.visualJudgments.size() == 1);
    CHECK(response.data.result.empty());
    const auto& record = response.data.visualJudgments[0];
    CHECK(record["event_code"] == "helmet");
    CHECK(record["algorithm_code"] == "helmet-0");
    CHECK(record["area_id"] == "Area-1");
    CHECK(record["provider"] == "laya_v");
    CHECK(Json(response).get<MsgDetectSend>().data.visualJudgments == response.data.visualJudgments);
    const int prepared = f.questions.count;
    auto again         = service.ProcessDetectGroup(input, error);
    REQUIRE(again.data.visualJudgments.size() == 1);
    CHECK(f.questions.count == prepared);
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Legacy picture JSON omits empty visual records including reused DTOs", "[visual-flow][picture]") {
    MsgPTaskDetectPicSend::ResData single;
    single.visualJudgments = {Json{{"status", "unknown"}}};
    Json j                 = single;
    REQUIRE(j.contains("visualJudgments"));
    single.visualJudgments.clear();
    to_json(j, single);
    CHECK_FALSE(j.contains("visualJudgments"));
    single.visualJudgments = {Json::object()};
    from_json(j, single);
    CHECK(single.visualJudgments.empty());
    MsgDetectSend::Data group;
    group.visualJudgments = {Json::object()};
    Json g                = group;
    group.visualJudgments.clear();
    to_json(g, group);
    CHECK_FALSE(g.contains("visualJudgments"));
    group.visualJudgments = {Json::object()};
    from_json(g, group);
    CHECK(group.visualJudgments.empty());
}

TEST_CASE("Picture publication rejects a stale earlier node and accepts shared run copies",
          "[visual-flow][picture]") {
    FlowFixture f;
    PTaskBase base;
    auto alg      = std::make_shared<ActionAlg>();
    alg->workFlow = {PictureVisualAction("first"), PictureVisualAction("later")};
    auto task     = base.TaskCreate("picture", alg);
    REQUIRE(task);
    class AfterJudgment final : public PActionBase {
    public:
        bool invalidate;
        AfterJudgment(ActionNode& action, bool stale) : PActionBase(action, "picture"), invalidate(stale) {}
        util::ErrorEnum HandPic(AlgDataPtr data) override {
            if (data->visualDecisions.empty())
                return util::ErrorEnum::Failed;
            if (invalidate)
                data->visualDecisions[0].run->Invalidate();
            else
                data->visualDecisions.push_back(data->visualDecisions[0]);
            return util::ErrorEnum::Success;
        }
    };
    const bool invalidate       = GENERATE(true, false);
    task->actions[1].actionInst = std::make_shared<AfterJudgment>(task->actions[1].action, invalidate);
    auto request                = PictureRequest();
    MsgPTaskDetectPicSend response;
    auto result = base.TaskDetectPic(task, request, response);
    if (invalidate) {
        CHECK(result == util::ErrorEnum::ActionStop);
        CHECK(response.resData.visualJudgments.empty());
    } else {
        CHECK(result == util::ErrorEnum::Success);
        CHECK(response.resData.visualJudgments.size() == 2);
    }
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Picture requests apply distinct configs atomically and snapshots remain readable during inference",
          "[visual-flow][picture][concurrency]") {
    FlowFixture f;
    PicTaskServiceImpl pictures;
    test::ScopedServiceOverride<IPicTaskDetect> detect(pictures);
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IPicTaskQuery>());
    auto alg      = std::make_shared<ActionAlg>();
    alg->workFlow = {PictureVisualAction()};
    REQUIRE(pictures.TaskCreate("picture", alg) == util::ErrorEnum::Success);
    REQUIRE(pictures.TaskStart("picture"));
    auto execute = [&](const std::string& areaId, double x) {
        auto request             = PictureRequest();
        request.taskId           = "picture";
        request.taskConfig.areas = {Region(areaId, x, 0.5)};
        MessageHandler handler;
        std::error_condition error;
        auto response = handler.Handle(std::move(request), error);
        return std::make_pair(error, response);
    };
    f.decisions.block = true;
    auto first        = std::async(std::launch::async, [&] { return execute("left", 0); });
    REQUIRE(f.decisions.Wait(1));
    auto snapshot = std::async(std::launch::async, [&] {
        MsgTaskConfig config;
        const bool found = pictures.GetTaskParam("picture", config);
        return std::make_pair(found, config);
    });
    REQUIRE(snapshot.wait_for(1s) == std::future_status::ready);
    auto saved = snapshot.get();
    REQUIRE(saved.first);
    REQUIRE(saved.second.areas.size() == 1);
    CHECK(saved.second.areas[0].areaId == "left");
    auto second = std::async(std::launch::async, [&] { return execute("right", 0.5); });
    CHECK(second.wait_for(40ms) == std::future_status::timeout);
    f.decisions.block = false;
    auto a            = first.get();
    auto b            = second.get();
    CHECK_FALSE(a.first);
    CHECK_FALSE(b.first);
    REQUIRE(a.second.resData.visualJudgments.size() == 1);
    REQUIRE(b.second.resData.visualJudgments.size() == 1);
    CHECK(a.second.resData.visualJudgments[0]["area_id"] == "left");
    CHECK(b.second.resData.visualJudgments[0]["area_id"] == "right");
    CHECK(a.second.resData.visualJudgments[0]["input_roi"] == Json::array({0, 0, 32, 64}));
    CHECK(b.second.resData.visualJudgments[0]["input_roi"] == Json::array({32, 0, 32, 64}));
    // Repeating the same request keeps the prepared question configuration.
    const int prepared = f.questions.count;
    auto repeat        = execute("right", 0.5);
    CHECK_FALSE(repeat.first);
    CHECK(f.questions.count == prepared);
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Concurrent grouped picture calls keep request areas and saved question catalog",
          "[visual-flow][picture][group][concurrency]") {
    FlowFixture f;
    test::MockAppInfoService info;
    test::ScopedServiceOverride<IAppInfoService> app(info);
    ALLOW_CALL(info, GetPicTaskGroupCount()).RETURN(1);
    PicTaskServiceImpl pictures;
    auto alg      = std::make_shared<ActionAlg>();
    alg->workFlow = {PictureVisualAction()};
    REQUIRE(pictures.TaskCreate("helmet-0", alg) == util::ErrorEnum::Success);
    REQUIRE(pictures.TaskStart("helmet-0"));
    auto run = [&](float x) {
        MsgDetectRecv input;
        input.eventCodes = {"helmet"};
        input.imageData  = "/9j/2Q==";
        MsgPTaskDetectExtParam ext;
        ext.eventCode = "helmet";
        MsgPTaskDetectExtParamRule rule{};
        rule.DetectRegion = {{x, 0}, {x + 0.5F, 0}, {x + 0.5F, 1}, {x, 1}};
        ext.rules         = {rule};
        input.extParam    = {ext};
        std::error_condition error;
        return pictures.ProcessDetectGroup(input, error);
    };
    f.decisions.block = true;
    auto first        = std::async(std::launch::async, [&] { return run(0); });
    REQUIRE(f.decisions.Wait(1));
    auto second = std::async(std::launch::async, [&] { return run(0.5); });
    CHECK(second.wait_for(40ms) == std::future_status::timeout);
    f.decisions.block = false;
    auto left         = first.get();
    auto right        = second.get();
    REQUIRE(left.data.visualJudgments.size() == 1);
    REQUIRE(right.data.visualJudgments.size() == 1);
    CHECK(left.data.visualJudgments[0]["input_roi"] == Json::array({0, 0, 32, 64}));
    CHECK(right.data.visualJudgments[0]["input_roi"] == Json::array({32, 0, 32, 64}));
    CHECK(left.data.visualJudgments[0]["request"]["config_revision"] !=
          right.data.visualJudgments[0]["request"]["config_revision"]);
    CHECK(f.llm.touched == 0);
}

TEST_CASE("Picture request isolation permits different tasks to infer concurrently",
          "[visual-flow][picture][concurrency]") {
    FlowFixture f;
    PicTaskServiceImpl pictures;
    auto alg      = std::make_shared<ActionAlg>();
    alg->workFlow = {PictureVisualAction()};
    REQUIRE(pictures.TaskCreate("first", alg) == util::ErrorEnum::Success);
    REQUIRE(pictures.TaskCreate("second", alg) == util::ErrorEnum::Success);
    REQUIRE(pictures.TaskStart("first"));
    REQUIRE(pictures.TaskStart("second"));
    auto execute = [&](const std::string& id) {
        auto input = PictureRequest();
        MsgPTaskDetectPicSend output;
        return pictures.DetectPic(id, input, output);
    };
    f.decisions.block = true;
    auto first        = std::async(std::launch::async, [&] { return execute("first"); });
    REQUIRE(f.decisions.Wait(1));
    auto second = std::async(std::launch::async, [&] { return execute("second"); });
    REQUIRE(f.decisions.Wait(2));
    f.decisions.block = false;
    CHECK(first.get() == util::ErrorEnum::Success);
    CHECK(second.get() == util::ErrorEnum::Success);
    CHECK(f.llm.touched == 0);
}
#endif
