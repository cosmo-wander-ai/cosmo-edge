#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"

#define private public
#include "flow/video/AiVideoQuality.h"
#undef private
#include "flow/video/AiVideoQualityMng.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "util/dto/ActionCodes.h"

namespace {

class TestableAiVideoQuality final : public cosmo::AiVideoQuality {
public:
    using AiVideoQuality::AiVideoQuality;

    [[nodiscard]] cosmo::util::ErrorEnum Status() const {
        return action_status;
    }
};

cosmo::ActionNode MakeVideoQualityAction() {
    cosmo::ActionNode action;
    action.actionId     = std::string(cosmo::AAVideoDiagnosis_Code);
    action.actionName   = std::string(cosmo::AAVideoDiagnosis_Name);
    action.flowActionId = "video-quality-flow";
    return action;
}

}  // namespace

TEST_CASE("AiVideoQuality reports unsupported without an analyser backend", "[video-quality][unsupported]") {
    auto action = MakeVideoQualityAction();
    TestableAiVideoQuality video_quality("video-quality-task", action);

    CHECK_FALSE(video_quality.AiSdkInit());
    CHECK(video_quality.Status() == cosmo::util::ErrorEnum::NotImplement);

    CHECK_FALSE(video_quality.AiSdkInit());
    CHECK(video_quality.Status() == cosmo::util::ErrorEnum::NotImplement);
}

TEST_CASE("AiVideoQuality unsupported lifecycle remains restartable",
          "[video-quality][unsupported][lifecycle]") {
    auto action = MakeVideoQualityAction();
    TestableAiVideoQuality video_quality("video-quality-task", action);

    REQUIRE(video_quality.Start());
    CHECK(video_quality.Status() == cosmo::util::ErrorEnum::NotImplement);
    std::vector<cosmo::AlgActionDataQueueStatus> statuses;
    video_quality.QueueStatus(statuses);
    REQUIRE(statuses.size() == 1);
    CHECK(statuses.front().actionStatus == cosmo::util::ErrorEnum::NotImplement);
    video_quality.Stop();

    REQUIRE(video_quality.Start());
    CHECK(video_quality.Status() == cosmo::util::ErrorEnum::NotImplement);
    video_quality.Stop();
}

TEST_CASE("AiVideoQuality drops input without publishing a synthetic result",
          "[video-quality][unsupported][fail-closed]") {
    auto action = MakeVideoQualityAction();
    TestableAiVideoQuality video_quality("video-quality-task", action);

    cosmo::AlgTaskUnit downstream;
    downstream.channel_id   = "channel";
    downstream.task_id      = "downstream-task";
    downstream.actionId     = "downstream-action";
    downstream.flowActionId = "downstream-flow";
    downstream.fps          = -1.0f;
    downstream.dataType     = cosmo::AlgDataType::TaskDataAiVideoQuality;
    downstream.que = std::make_shared<cosmo::AlgDataQueue<cosmo::AlgDataPtr>>("video-quality-downstream");
    REQUIRE(video_quality.RegistTaskQueue(downstream));

    constexpr int width      = 16;
    constexpr int height     = 16;
    constexpr int frame_size = width * height * 3 / 2;
    cosmo::mem::MemoryPoolMng memory_pool(std::make_unique<cosmo::mem::AllocatorCpu>(), {frame_size});
    cosmo::mem::SetMemoryPoolContext(&memory_pool);
    struct PoolReset {
        ~PoolReset() {
            cosmo::mem::SetMemoryPoolContext(nullptr);
        }
    } pool_reset;

    auto frame = std::make_shared<cosmo::media::VideoFrame>(width, height,
                                                            cosmo::media::PixelFormat::PIXEL_I420, 42, 1000);
    REQUIRE(VideoFrameValid(frame));
    std::fill_n(frame->GetData(), frame->GetSize(), static_cast<std::uint8_t>(128));

    auto input                         = std::make_shared<cosmo::AlgData>();
    input->dataType                    = cosmo::AlgDataType::ChannelDataDec;
    input->chanDataDec.frame           = frame;
    input->chanDataDec.reportTimeStamp = 1000;
    video_quality.HandFrame(input);

    CHECK(video_quality.Status() == cosmo::util::ErrorEnum::NotImplement);
    CHECK(downstream.que->RestSize() == 0);
    CHECK_FALSE(input->bHaveClassify);
    CHECK(input->dataType == cosmo::AlgDataType::ChannelDataDec);
    CHECK(input->taskResults.empty());
    CHECK(input->chanDataDec.frame == frame);
    REQUIRE(video_quality.RemoveTaskQueue(downstream));
}

TEST_CASE("AiVideoQuality returns empty overview without service dependencies",
          "[video-quality][unsupported]") {
    auto action = MakeVideoQualityAction();
    TestableAiVideoQuality video_quality("video-quality-task", action);

    const auto overview = video_quality.GetOverviewInfo("channel", "video-quality-task");
    CHECK(overview.aiFrames.empty());
}

TEST_CASE("AiVideoQuality manager preserves unsupported action lifecycle",
          "[video-quality][unsupported][manager]") {
    auto action = MakeVideoQualityAction();
    cosmo::AiVideoQualityMng manager;

    const auto first  = manager.GetInst("video-quality-task", action);
    const auto second = manager.GetInst("video-quality-task", action);
    REQUIRE(first);
    CHECK(first == second);
    CHECK_FALSE(first->AiSdkInit());
    CHECK(manager.DeleteInst(first, "video-quality-task"));
}
