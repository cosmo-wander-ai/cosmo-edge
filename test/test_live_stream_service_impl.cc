#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nlohmann/json.hpp"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"

#define private public
#include "flow/stream/StreamViewer.h"
#include "service/media/impl/LiveStreamServiceImpl.h"
#undef private

#include "flow/channel/AlgChannel.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockCameraService.h"
#include "support/MockDefaults.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;

namespace {

struct LiveStreamDependencies {
    cosmo::test::MockCameraService cameraSvc;
    cosmo::test::MockAppInfoService appInfoSvc;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::ScopedServiceOverride<ICameraTaskConfig> cameraTaskConfig{cameraSvc};
    cosmo::test::ScopedServiceOverride<ICameraChannelQuery> cameraChannelQuery{cameraSvc};
    cosmo::test::ScopedServiceOverride<IAppInfoService> appInfo{appInfoSvc};

    LiveStreamDependencies() {
        cosmo::test::AllowPreviewChannelDefaults(cameraSvc, expectations);
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetNumber()).RETURN(1));
    }
};

}  // namespace

TEST_CASE("LiveStreamServiceImpl: 视频流管理核心逻辑", "[live-stream]") {
    LiveStreamDependencies mocks;
    LiveStreamServiceImpl sut;

    SECTION("ViewerCreate 返回 CameraNotExist 当 Channel 不存在") {
        ALLOW_CALL(mocks.cameraSvc, GetChannelInst(trompeloeil::_)).RETURN(nullptr);
        cosmo::LiveStream::LiveStreamInfo streamInfo;
        REQUIRE(sut.ViewerCreate("non_exist_channel", "alg_code", streamInfo) ==
                cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("ViewerCreate 拒绝未绑定到 Channel 的算法") {
        cosmo::ActionNode dummyAction;
        auto mockChannel =
            std::make_shared<cosmo::AlgChannel>("channel_1", "task_1", dummyAction, "rtsp://url");
        ALLOW_CALL(mocks.cameraSvc, GetChannelInst("channel_1")).RETURN(mockChannel);
        ALLOW_CALL(mocks.cameraSvc, GetTasks("channel_1"))
            .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{});

        cosmo::LiveStream::LiveStreamInfo streamInfo;
        REQUIRE(sut.ViewerCreate("channel_1", "invalid_alg", streamInfo) ==
                cosmo::util::ErrorEnum::TaskNotExist);
    }

    SECTION("ViewerCreate 快速拒绝已停止的 OSD 任务") {
        cosmo::ActionNode dummyAction;
        auto mockChannel =
            std::make_shared<cosmo::AlgChannel>("channel_1", "task_1", dummyAction, "rtsp://url");
        ALLOW_CALL(mocks.cameraSvc, GetChannelInst("channel_1")).RETURN(mockChannel);
        ALLOW_CALL(mocks.cameraSvc, GetTasks("channel_1"))
            .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{{"", "alg_code", "", "", "", false}});
        FORBID_CALL(mocks.cameraSvc, AcquirePreviewChannel(trompeloeil::_));

        cosmo::LiveStream::LiveStreamInfo streamInfo;
        REQUIRE(sut.ViewerCreate("channel_1", "alg_code", streamInfo) == cosmo::util::ErrorEnum::ActionStop);
    }

    SECTION("ViewerHeartBeat 返回 CameraNotExist 当 Channel 不存在") {
        ALLOW_CALL(mocks.cameraSvc, GetChannelInst(trompeloeil::_)).RETURN(nullptr);
        REQUIRE(sut.ViewerHeartBeat("non_exist_channel", "alg_code") ==
                cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("ViewerHeartBeat 在算法任务停止后回收预览") {
        cosmo::ActionNode dummyAction;
        auto mockChannel =
            std::make_shared<cosmo::AlgChannel>("channel_1", "task_1", dummyAction, "rtsp://url");
        ALLOW_CALL(mocks.cameraSvc, GetChannelInst("channel_1")).RETURN(mockChannel);
        cosmo::service::camera::CameraTaskDto task;
        task.algorithmCode = "alg_code";
        task.enable        = false;
        ALLOW_CALL(mocks.cameraSvc, GetTasks("channel_1"))
            .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{task});
        REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("channel_1"));

        auto viewer = std::make_shared<cosmo::StreamViewer>(mockChannel, "channel_1", "alg_code");
        viewer->MarkReady(std::chrono::nanoseconds::zero());
        sut.viewers_.push_back(viewer);

        REQUIRE(sut.ViewerHeartBeat("channel_1", "alg_code") == cosmo::util::ErrorEnum::ActionStop);
        REQUIRE(sut.viewers_.empty());
    }

    SECTION("ViewerDelete 可以安全处理不存在的 Viewer") {
        REQUIRE(sut.ViewerDelete("non_exist_channel", "alg_code") == true);
    }

    SECTION("启动中的多客户端仅在最后一个客户端离开时取消") {
        auto gate                    = std::make_shared<LiveStreamServiceImpl::ViewerStartGate>();
        gate->participants           = 2;
        gate->channel_id             = "pending_channel";
        gate->channel_lease_acquired = true;
        const std::string key        = "pending_channel\npending_alg";
        sut.starting_viewers_[key]   = gate;
        REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("pending_channel"));

        REQUIRE(sut.ViewerDelete("pending_channel", "pending_alg"));
        REQUIRE(gate->participants == 1);
        REQUIRE_FALSE(gate->cancelled);

        REQUIRE(sut.ViewerDelete("pending_channel", "pending_alg"));
        REQUIRE(gate->participants == 0);
        REQUIRE(gate->cancelled);
        REQUIRE_FALSE(gate->channel_lease_acquired);
    }

    SECTION("SetViewCounts 不发生崩溃") {
        sut.SetViewCounts(16);
        // 这仅仅是个 setter，保证无异常抛出即可
        REQUIRE(true);
    }

    SECTION("心跳超时断开及重连全流程 (通过内部状态测试)") {
        cosmo::ActionNode dummyAction;
        auto mockChannel =
            std::make_shared<cosmo::AlgChannel>("channel_1", "task_1", dummyAction, "rtsp://url");
        mockChannel->demuxer_.action_status_ = cosmo::util::ErrorEnum::Success;
        ALLOW_CALL(mocks.cameraSvc, GetChannelInst("channel_1")).RETURN(mockChannel);
        ALLOW_CALL(mocks.cameraSvc, GetTasks("channel_1"))
            .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{{"", "alg_1", "", "", "", true}});

        // 我们不直接调用 ViewerCreate, 因为 WaitReady 会阻塞等数据
        // 直接构造并放入 m_viewers 模拟已连接
        auto viewer = std::make_shared<cosmo::StreamViewer>(mockChannel, "channel_1", "alg_1");
        viewer->HeartBeat();
        REQUIRE_FALSE(viewer->HeartBeatCheck());
        REQUIRE_FALSE(viewer->IsPublishReady());
        {
            std::unique_lock<std::shared_mutex> lock(sut.mtx_);
            sut.viewers_.push_back(viewer);
        }

        // 验证初始状态存在
        {
            std::shared_lock<std::shared_mutex> lock(sut.mtx_);
            REQUIRE(sut.viewers_.size() == 1);
        }

        // 修改 viewer 的心跳状态使其强制超时
        viewer->heartbeat_timestamp_    = cosmo::util::GetMilliseconds() - 100000;
        viewer->heartbeat_failed_count_ = 10;

        // 手动调用内部方法，不等待 watchdog thread
        sut.CheckAliveTasks();

        // 验证超时后已被清理
        {
            std::shared_lock<std::shared_mutex> lock(sut.mtx_);
            REQUIRE(sut.viewers_.size() == 0);
        }

        // Keepalive must expose the missing viewer so the client can recreate
        // the stream instead of accepting a permanently stale session.
        REQUIRE(sut.ViewerHeartBeat("channel_1", "alg_1") == cosmo::util::ErrorEnum::DemuxNoData);
    }
}

TEST_CASE("LiveStreamServiceImpl: Stop is idempotent and rejects new viewer work",
          "[live-stream][lifecycle]") {
    LiveStreamServiceImpl sut;
    REQUIRE_NOTHROW(sut.Stop());
    REQUIRE_NOTHROW(sut.Stop());

    cosmo::LiveStream::LiveStreamInfo stream_info;
    REQUIRE(sut.ViewerCreate("channel", "algorithm", stream_info) == cosmo::util::ErrorEnum::SysErr);
    REQUIRE(sut.ViewerHeartBeat("channel", "algorithm") == cosmo::util::ErrorEnum::SysErr);
    REQUIRE(sut.ViewerDelete("channel", "algorithm"));
    REQUIRE_NOTHROW(sut.SetViewCounts(1));
}

TEST_CASE("LiveStreamServiceImpl: final ready viewer releases exactly one channel lease",
          "[live-stream][preview][regression]") {
    LiveStreamDependencies mocks;
    LiveStreamServiceImpl sut;
    sut.is_running_.store(false);
    sut.watchdog_cv_.notify_all();
    sut.watchdog_thread_.join();

    cosmo::ActionNode action;
    auto channel = std::make_shared<cosmo::AlgChannel>("ready", "task", action, "rtsp://url");
    auto viewer  = std::make_shared<cosmo::StreamViewer>(channel, "ready", "alg");
    viewer->UpViewerNum();
    sut.viewers_.push_back(viewer);

    REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("ready")).TIMES(1);
    REQUIRE(sut.ViewerDelete("ready", "alg"));
    REQUIRE(viewer->GetViewerNum() == 1);
    REQUIRE_FALSE(viewer->IsStopped());
    REQUIRE(sut.ViewerDelete("ready", "alg"));
    REQUIRE(viewer->IsStopped());
    REQUIRE(sut.viewers_.empty());
    REQUIRE(sut.retiring_viewers_.empty());
    REQUIRE(sut.ViewerDelete("ready", "alg"));
}

TEST_CASE("LiveStreamServiceImpl: stale and legacy stops cannot consume scoped replacement leases",
          "[live-stream][preview][regression]") {
    LiveStreamDependencies mocks;
    LiveStreamServiceImpl sut;
    sut.is_running_.store(false);
    sut.watchdog_cv_.notify_all();
    sut.watchdog_thread_.join();

    cosmo::ActionNode action;
    auto channel = std::make_shared<cosmo::AlgChannel>("camera", "task", action, "rtsp://url");
    auto viewer  = std::make_shared<cosmo::StreamViewer>(channel, "camera", "alg");
    viewer->UpViewerNum();
    sut.viewers_.push_back(viewer);
    sut.viewer_session_ids_["camera\nalg"] = {{"new-client-A", std::chrono::steady_clock::now()},
                                              {"new-client-B", std::chrono::steady_clock::now()}};
    REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("camera")).TIMES(1);

    REQUIRE(sut.ViewerDelete("camera", "alg", "old-client"));
    REQUIRE(sut.ViewerDelete("camera", "alg"));
    REQUIRE(viewer->GetViewerNum() == 2);
    REQUIRE(sut.ViewerHeartBeat("camera", "alg", "old-client") == cosmo::util::ErrorEnum::LiveStreamStopped);
    REQUIRE(sut.ViewerDelete("camera", "alg", "new-client-A"));
    REQUIRE(sut.ViewerDelete("camera", "alg", "new-client-A"));
    REQUIRE(sut.ViewerDelete("camera", "alg"));
    REQUIRE(viewer->GetViewerNum() == 1);
    REQUIRE_FALSE(viewer->IsStopped());
    REQUIRE(sut.ViewerDelete("camera", "alg", "new-client-B"));
    REQUIRE(sut.ViewerDelete("camera", "alg", "new-client-B"));
    REQUIRE(viewer->IsStopped());
    REQUIRE(sut.viewer_session_ids_.empty());
}

TEST_CASE("LiveStreamServiceImpl: startup cancellation is scoped to the requesting acquisition",
          "[live-stream][preview][regression]") {
    LiveStreamDependencies mocks;
    LiveStreamServiceImpl sut;
    sut.is_running_.store(false);
    sut.watchdog_cv_.notify_all();
    sut.watchdog_thread_.join();

    auto gate                             = std::make_shared<LiveStreamServiceImpl::ViewerStartGate>();
    gate->participants                    = 2;
    gate->session_ids                     = {"client-A", "client-B"};
    gate->channel_id                      = "pending";
    gate->channel_lease_acquired          = true;
    sut.starting_viewers_["pending\nalg"] = gate;
    REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("pending")).TIMES(1);

    REQUIRE(sut.ViewerDelete("pending", "alg", "stale-client"));
    REQUIRE(sut.ViewerDelete("pending", "alg"));
    REQUIRE(gate->participants == 2);
    REQUIRE(sut.ViewerDelete("pending", "alg", "client-A"));
    REQUIRE(sut.ViewerDelete("pending", "alg", "client-A"));
    REQUIRE(gate->participants == 1);
    REQUIRE_FALSE(gate->cancelled);
    REQUIRE(sut.ViewerDelete("pending", "alg", "client-B"));
    REQUIRE(gate->participants == 0);
    REQUIRE(gate->cancelled);
    REQUIRE_FALSE(gate->channel_lease_acquired);
}

TEST_CASE("LiveStreamServiceImpl: retirement keeps only its own key reserved during lease release",
          "[live-stream][preview][concurrency]") {
    LiveStreamDependencies mocks;
    LiveStreamServiceImpl sut;
    sut.is_running_.store(false);
    sut.watchdog_cv_.notify_all();
    sut.watchdog_thread_.join();

    cosmo::ActionNode action;
    auto channel = std::make_shared<cosmo::AlgChannel>("retiring", "task", action, "rtsp://url");
    auto viewer  = std::make_shared<cosmo::StreamViewer>(channel, "retiring", "alg");
    sut.viewers_.push_back(viewer);
    std::mutex mutex;
    std::condition_variable cv;
    bool releasing        = false;
    bool complete_release = false;
    REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("retiring")).LR_SIDE_EFFECT({
        std::unique_lock<std::mutex> lock(mutex);
        releasing = true;
        cv.notify_all();
        cv.wait(lock, [&] { return complete_release; });
    });
    std::thread retiring([&] { sut.ViewerDelete("retiring", "alg"); });
    bool saw_releasing = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        saw_releasing = cv.wait_for(lock, std::chrono::seconds(2), [&] { return releasing; });
    }
    bool key_reserved        = false;
    bool other_key_available = false;
    if (saw_releasing) {
        {
            std::shared_lock<std::shared_mutex> lock(sut.mtx_);
            key_reserved = sut.retiring_viewers_.count("retiring\nalg") == 1 && sut.viewers_.empty();
        }
        other_key_available = sut.ViewerDelete("unrelated", "alg");
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        complete_release = true;
    }
    cv.notify_all();
    retiring.join();
    REQUIRE(saw_releasing);
    REQUIRE(key_reserved);
    REQUIRE(other_key_available);
    REQUIRE(sut.retiring_viewers_.empty());
}

TEST_CASE("LiveStream DTOs preserve optional preview acquisition identity", "[live-stream][dto]") {
    const nlohmann::json scoped = {
        {"channelId", "camera"}, {"algorithmId", "alg"}, {"previewSessionId", "per-acquire-uuid"}};
    auto create    = scoped.get<cosmo::LiveStream::MsgRequestLiveStreamRecv>();
    auto heartbeat = scoped.get<cosmo::LiveStream::MsgStreamKeepAliveRecv>();
    auto stop      = scoped.get<cosmo::LiveStream::MsgStreamStopRecv>();
    REQUIRE(create.previewSessionId == "per-acquire-uuid");
    REQUIRE(heartbeat.previewSessionId == create.previewSessionId);
    REQUIRE(stop.previewSessionId == create.previewSessionId);
    REQUIRE(nlohmann::json(create)["previewSessionId"] == create.previewSessionId);
    REQUIRE(nlohmann::json(heartbeat)["previewSessionId"] == create.previewSessionId);
    REQUIRE(nlohmann::json(stop)["previewSessionId"] == create.previewSessionId);
    REQUIRE(nlohmann::json({{"channelId", "camera"}, {"algorithmId", "alg"}})
                .get<cosmo::LiveStream::MsgStreamStopRecv>()
                .previewSessionId.empty());
}

TEST_CASE("LiveStreamServiceImpl: watchdog cancels disabled startup without waiting for client heartbeat",
          "[live-stream][preview][regression]") {
    LiveStreamDependencies mocks;
    LiveStreamServiceImpl sut;
    sut.is_running_.store(false);
    sut.watchdog_cv_.notify_all();
    sut.watchdog_thread_.join();

    cosmo::ActionNode action;
    auto channel = std::make_shared<cosmo::AlgChannel>("pending", "task", action, "rtsp://url");
    channel->demuxer_.action_status_ = cosmo::util::ErrorEnum::Success;
    channel->demuxer_.status_.status = cosmo::service::camera::AlgDemuxStatus::AlgDemuxReading;
    channel->demuxer_.demuxer_.width_.store(1920);
    channel->demuxer_.demuxer_.height_.store(1080);
    channel->demuxer_.demuxer_.fps_.store(25.0F);
    ALLOW_CALL(mocks.cameraSvc, GetChannelInst("pending")).RETURN(channel);

    int task_queries              = 0;
    bool enabled                  = true;
    bool cancelled_while_reserved = false;
    // Trigger the watchdog from the real create path's post-reservation task
    // check, before it builds a publisher. Do not hand-fill the gate identity.
    ALLOW_CALL(mocks.cameraSvc, GetTasks("pending")).LR_RETURN(([&] {
        if (++task_queries == 2) {
            enabled = false;
            sut.CheckAliveTasks();
            {
                std::shared_lock<std::shared_mutex> lock(sut.mtx_);
                const auto gate = sut.starting_viewers_.find("pending\ndisabled");
                cancelled_while_reserved =
                    gate != sut.starting_viewers_.end() && gate->second->cancelled &&
                    gate->second->cancel_result == cosmo::util::ErrorEnum::ActionStop &&
                    !gate->second->channel_lease_acquired;
            }
            sut.CheckAliveTasks();
        }
        return std::vector<cosmo::service::camera::CameraTaskDto>{{"", "disabled", "", "", "", enabled}};
    })());
    REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("pending")).TIMES(1);

    cosmo::LiveStream::LiveStreamInfo stream_info;
    REQUIRE(sut.ViewerCreate("pending", "disabled", "client", stream_info) ==
            cosmo::util::ErrorEnum::ActionStop);
    REQUIRE(cancelled_while_reserved);
    REQUIRE(sut.starting_viewers_.empty());
    REQUIRE(sut.viewers_.empty());
}

TEST_CASE("LiveStreamServiceImpl: one client's heartbeat cannot retain another expired lease",
          "[live-stream][preview][regression]") {
    LiveStreamDependencies mocks;
    LiveStreamServiceImpl sut;
    sut.is_running_.store(false);
    sut.watchdog_cv_.notify_all();
    sut.watchdog_thread_.join();

    const auto output_path = std::filesystem::temp_directory_path() /
                             ("cosmo-preview-lease-" + cosmo::util::GenerateUUID() + ".flv");
    struct RemoveOutput {
        std::filesystem::path path;
        ~RemoveOutput() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } remove_output{output_path};
    cosmo::ActionNode action;
    auto channel          = std::make_shared<cosmo::AlgChannel>("camera", "task", action, "rtsp://url");
    auto viewer           = std::make_shared<cosmo::StreamViewer>(channel, "camera", "");
    viewer->video_pusher_ = std::make_shared<cosmo::RtmpStreamPusher>(cosmo::media::VideoCodecType::kH264,
                                                                      output_path.string(), 16, 16, 1.0F);
    // Valid one-frame Annex-B access unit; makes the real file-backed publisher ready.
    const std::vector<uint8_t> keyframe{
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x7b, 0x01, 0x10, 0x00, 0x00, 0x03, 0x00,
        0x10, 0x00, 0x00, 0x03, 0x00, 0x28, 0xf1, 0x22, 0x6a, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x0f,
        0xc8, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09, 0x02, 0xe0};
    viewer->video_pusher_->PushFrame(keyframe.data(), keyframe.size());
    REQUIRE(viewer->IsPublishReady());
    sut.viewers_.push_back(viewer);
    const auto old_heartbeat            = std::chrono::steady_clock::now() - std::chrono::seconds(61);
    sut.viewer_session_ids_["camera\n"] = {{"abandoned-A", old_heartbeat}};
    ALLOW_CALL(mocks.cameraSvc, GetChannelInst("camera")).RETURN(channel);

    SECTION("heartbeats refresh only the matching acquisition") {
        viewer->UpViewerNum();
        const auto recent_heartbeat = std::chrono::steady_clock::now() - std::chrono::seconds(30);
        sut.viewer_session_ids_["camera\n"].emplace("active-B", recent_heartbeat);
        REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("camera")).TIMES(1);

        REQUIRE(sut.ViewerHeartBeat("camera", "", "active-B") == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.viewer_session_ids_["camera\n"].at("abandoned-A") == old_heartbeat);
        REQUIRE(sut.viewer_session_ids_["camera\n"].at("active-B") > recent_heartbeat);
        sut.CheckAliveTasks();
        REQUIRE(viewer->GetViewerNum() == 1);
        REQUIRE(sut.viewer_session_ids_["camera\n"].count("abandoned-A") == 0);
        REQUIRE_FALSE(viewer->IsStopped());
        REQUIRE(sut.ViewerDelete("camera", "", "active-B"));
        REQUIRE(viewer->IsStopped());
        REQUIRE(sut.viewers_.empty());
        REQUIRE(sut.ViewerDelete("camera", "", "abandoned-A"));
    }

    SECTION("new acquisition survives the previous client's heartbeat cleanup boundary") {
        channel->demuxer_.action_status_ = cosmo::util::ErrorEnum::Success;
        channel->demuxer_.status_.status = cosmo::service::camera::AlgDemuxStatus::AlgDemuxReading;
        channel->demuxer_.demuxer_.width_.store(1920);
        channel->demuxer_.demuxer_.height_.store(1080);
        channel->demuxer_.demuxer_.fps_.store(25.0F);
        viewer->heartbeat_timestamp_    = cosmo::util::GetMilliseconds() - 61000;
        viewer->heartbeat_failed_count_ = 5;
        // One temporary create lease, then the shared viewer's final lease.
        REQUIRE_CALL(mocks.cameraSvc, ReleasePreviewChannel("camera")).TIMES(2);
        cosmo::LiveStream::LiveStreamInfo stream_info;
        const auto acquired_after = std::chrono::steady_clock::now();
        REQUIRE(sut.ViewerCreate("camera", "", "fresh-B", stream_info) == cosmo::util::ErrorEnum::Success);
        REQUIRE(stream_info.previewSessionId == "fresh-B");
        REQUIRE(sut.viewer_session_ids_["camera\n"].at("fresh-B") >= acquired_after);
        // No heartbeat from B yet: its first one is scheduled ten seconds later.
        sut.CheckAliveTasks();
        REQUIRE_FALSE(viewer->IsStopped());
        REQUIRE(viewer->GetViewerNum() == 1);
        REQUIRE(sut.viewer_session_ids_["camera\n"].count("abandoned-A") == 0);
        REQUIRE(sut.ViewerDelete("camera", "", "fresh-B"));
        REQUIRE(viewer->IsStopped());
        REQUIRE(sut.viewers_.empty());
    }
}
