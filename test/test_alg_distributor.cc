#include "catch_amalgamated.hpp"
/*
 * test_alg_distributor.cc - AlgDataQueueDistributor 单元测试
 */
#include "flow/common/AlgDataQueueDistributor.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "util/dto/ActionCodes.h"

using namespace cosmo;

static AlgTaskUnit makeTask(const std::string& chId, const std::string& tId, const std::string& aId,
                            float fps = 5.0) {
    AlgTaskUnit task;
    task.channel_id   = chId;
    task.task_id      = tId;
    task.actionId     = aId;
    task.flowActionId = aId;
    task.fps          = fps;
    task.que          = std::make_shared<AlgDataQueue<AlgDataPtr>>(tId + "_q", 20);
    return task;
}

TEST_CASE("AlgDataQueueDistributor RegistProcQueue", "[Distributor]") {
    AlgDataQueueDistributor dist("test_dist");

    auto task1 = makeTask("ch1", "t1", "a1", 5.0);
    REQUIRE(dist.RegistProcQueue(task1));

    SECTION("GetMaxFps returns registered FPS") {
        REQUIRE(dist.GetMaxFps() >= 5.0f);
    }

    SECTION("GetBindTasks returns registered tasks") {
        auto tasks = dist.GetBindTasks();
        REQUIRE(tasks.size() >= 1);
    }
}

TEST_CASE("AlgDataQueueDistributor RemoveProcQueue", "[Distributor]") {
    AlgDataQueueDistributor dist("remove_dist");

    auto task1 = makeTask("ch1", "t1", "a1", 5.0);
    dist.RegistProcQueue(task1);

    auto task2 = makeTask("ch2", "t2", "a2", 10.0);
    dist.RegistProcQueue(task2);

    SECTION("Remove and verify") {
        dist.RemoveProcQueue(task1);
        auto tasks = dist.GetBindTasks();
        // After removal, should have fewer groups or tasks
        bool found = false;
        for (const auto& group : tasks) {
            for (const auto& t : group.tasks) {
                if (t.task_id == "t1")
                    found = true;
            }
        }
        REQUIRE_FALSE(found);
    }
}

TEST_CASE("AlgDataQueueDistributor multiple FPS", "[Distributor]") {
    AlgDataQueueDistributor dist("fps_dist");

    auto task1 = makeTask("ch1", "t1", "a1", 2.0);
    auto task2 = makeTask("ch1", "t2", "a2", 8.0);
    dist.RegistProcQueue(task1);
    dist.RegistProcQueue(task2);

    REQUIRE(dist.GetMaxFps() >= 8.0f);
}

TEST_CASE("AlgDataQueueDistributor sign changes", "[Distributor]") {
    AlgDataQueueDistributor dist("sign_dist");
    int sign0 = dist.GetSign();

    auto task1 = makeTask("ch1", "t1", "a1");
    dist.RegistProcQueue(task1);
    int sign1 = dist.GetSign();
    REQUIRE(sign1 > sign0);

    dist.RemoveProcQueue(task1);
    int sign2 = dist.GetSign();
    REQUIRE(sign2 > sign1);
}

TEST_CASE("AlgDataQueueDistributor plans before host frame materialization", "[Distributor]") {
    AlgDataQueueDistributor dist("deferred_dist");
    auto task = makeTask("ch1", "deferred", "a1", -1.0f);
    REQUIRE(dist.RegistProcQueue(task));

    auto input            = std::make_shared<AlgData>();
    input->firstTimePoint = std::chrono::steady_clock::now();
    auto ready_plan       = dist.PrepareFrameDistribution(input);
    REQUIRE_FALSE(ready_plan.Empty());
    REQUIRE(ready_plan.queues.size() == 1);
    CHECK_FALSE(ready_plan.SupportsNativeInference());

    for (size_t index = 0; index < task.que->GetMaxSize(); ++index) {
        REQUIRE(task.que->Insert(std::make_shared<AlgData>()));
    }
    input->firstTimePoint     = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto saturated_plan = dist.PrepareFrameDistribution(input);
    CHECK(saturated_plan.Empty());

    AlgDataQueueInfo status;
    REQUIRE(task.que->Status(status));
    CHECK(status.status.discardCount >= 1);
}

TEST_CASE("Eligible detection, classification and recognition queues share native frames", "[Distributor]") {
    AlgDataQueueDistributor detector_dist("native_detector_dist");
    auto detector = makeTask("ch1", "detector", std::string(AADetect_Code), -1.0f);
    REQUIRE(detector_dist.RegistProcQueue(detector));

    auto input               = std::make_shared<AlgData>();
    input->firstTimePoint    = std::chrono::steady_clock::now();
    const auto detector_plan = detector_dist.PrepareFrameDistribution(input);
    CHECK(detector_plan.SupportsNativeInference());

    AlgDataQueueDistributor mixed_dist("native_mixed_dist");
    const auto code   = GENERATE(AAClassify_Code, AARecognizer_Code);
    auto non_detector = makeTask("ch1", "inference", std::string(code), -1.0f);
    REQUIRE(mixed_dist.RegistProcQueue(non_detector));
    input->firstTimePoint = std::chrono::steady_clock::now();
    CHECK(mixed_dist.PrepareFrameDistribution(input).SupportsNativeInference());
    REQUIRE(mixed_dist.RegistProcQueue(detector));
    auto unrelated = makeTask("ch1", "other", std::string(AAVideoDiagnosis_Code), -1.0f);
    REQUIRE(mixed_dist.RegistProcQueue(unrelated));
    input->firstTimePoint = std::chrono::steady_clock::now();
    const auto mixed_plan = mixed_dist.PrepareFrameDistribution(input);
    CHECK(mixed_plan.SupportsNativeInference());
}

TEST_CASE("Native inference descriptors are isolated across detector queues", "[Distributor]") {
    AlgDataQueueDistributor dist("native_multi_detector_dist");
    auto detector_a = makeTask("ch1", "detector-a", std::string(AADetect_Code), -1.0f);
    auto detector_b = makeTask("ch1", "detector-b", std::string(AADetect_Code), -1.0f);
    REQUIRE(dist.RegistProcQueue(detector_a));
    REQUIRE(dist.RegistProcQueue(detector_b));

    auto input            = std::make_shared<AlgData>();
    input->firstTimePoint = std::chrono::steady_clock::now();
    const auto plan       = dist.PrepareFrameDistribution(input);
    REQUIRE(plan.SupportsNativeInference());
    REQUIRE(plan.queues.size() == 2);

    auto native           = std::make_shared<media::NativeVideoBuffer>();
    native->fd            = 7;
    native->bytes         = 24;
    native->width         = 4;
    native->height        = 4;
    native->width_stride  = 4;
    native->height_stride = 4;
    native->format        = media::NativeVideoBufferFormat::NV12;
    native->owner         = std::make_shared<int>(1);

    constexpr int pool_size = 4 * 4 * 3 / 2;
    mem::MemoryPoolMng memory_pool(std::make_unique<mem::AllocatorCpu>(), {pool_size});
    mem::SetMemoryPoolContext(&memory_pool);
    struct PoolReset {
        ~PoolReset() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } pool_reset;
    auto frame = std::make_shared<media::VideoFrame>(4, 4, media::PixelFormat::PIXEL_I420);

    REQUIRE(dist.DistributorPreparedFrame(plan, input, frame,
                                          [native](AlgDataPtr source, VideoFramePtr converted_frame) {
                                              auto converted               = AlgDataCopy(source);
                                              converted->chanDataDec.frame = std::move(converted_frame);
                                              converted->chanDataDec.native_buffer = native;
                                              return converted;
                                          }) == 2);

    auto queued_a = detector_a.que->Pop();
    auto queued_b = detector_b.que->Pop();
    REQUIRE(queued_a);
    REQUIRE(queued_b);
    CHECK(queued_a != queued_b);
    REQUIRE(queued_a->chanDataDec.native_buffer);
    REQUIRE(queued_b->chanDataDec.native_buffer);
    queued_a->chanDataDec.native_buffer.reset();
    CHECK(queued_b->chanDataDec.native_buffer == native);
}

TEST_CASE("Channel lifecycle queues do not force host frame materialization", "[Distributor]") {
    AlgDataQueueDistributor dist("channel_metadata_dist");
    auto channel_task = makeTask("ch1", "ch1-ChannelTask", std::string(BAStreamChannel_Code), -1.0f);
    REQUIRE(dist.RegistProcQueue(channel_task));

    auto input            = std::make_shared<AlgData>();
    input->dataType       = AlgDataType::ChannelDataOrig;
    input->firstTimePoint = std::chrono::steady_clock::now();
    const auto plan       = dist.PrepareFrameDistribution(input);

    CHECK(plan.Empty());
    CHECK(channel_task.que->RestSize() == 1);
    CHECK(channel_task.que->Pop() == input);
}
