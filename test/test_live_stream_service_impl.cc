#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "catch_amalgamated.hpp"
#include "util/TimeUtil.h"

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
        cosmo::service::camera::CameraTaskDto task;
        task.algorithmCode = "alg_code";
        task.enable        = false;
        ALLOW_CALL(mocks.cameraSvc, GetTasks("channel_1"))
            .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{task});
        FORBID_CALL(mocks.cameraSvc, AcquirePreviewChannel(trompeloeil::_));

        cosmo::LiveStream::LiveStreamInfo streamInfo;
        REQUIRE(sut.ViewerCreate("channel_1", "alg_code", streamInfo) == cosmo::util::ErrorEnum::ActionStop);
    }

    SECTION("ViewerHeartBeat 返回 CameraNotExist 当 Channel 不存在") {
        ALLOW_CALL(mocks.cameraSvc, GetChannelInst(trompeloeil::_)).RETURN(nullptr);
        REQUIRE(sut.ViewerHeartBeat("non_exist_channel", "alg_code") ==
                cosmo::util::ErrorEnum::CameraNotExist);
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
