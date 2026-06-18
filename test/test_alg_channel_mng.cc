#include "catch_amalgamated.hpp"
/*
 * test_alg_channel_mng.cc - AlgChannelMng 单元测试
 */
#include "flow/channel/AlgChannelMng.h"
#include "util/dto/CameraMsgTypes.h"

using namespace cosmo;

TEST_CASE("AlgChannelMng initial state", "[AlgChannelMng]") {
    AlgChannelMng channelMng;

    SECTION("GetChannelInst for non-existent returns nullptr") {
        auto inst = channelMng.GetChannelInst("no_such_channel");
        REQUIRE(inst == nullptr);
    }

    SECTION("GetChannelTasks for non-existent returns empty") {
        auto tasks = channelMng.GetChannelTasks("no_such_channel");
        REQUIRE(tasks.empty());
    }

    SECTION("QueueStatus initially empty") {
        std::vector<AlgActionDataQueueStatus> queStatus;
        channelMng.QueueStatus(queStatus);
        REQUIRE(queStatus.empty());
    }

    SECTION("ActionInfo initially empty") {
        std::vector<ActionRuntimeInfo> infos;
        channelMng.ActionInfo(infos);
        REQUIRE(infos.empty());
    }

    SECTION("GetCameraInfo initially empty") {
        std::vector<cosmo::MsgCameraInfo> cameraInfos;
        channelMng.GetCameraInfo(cameraInfos);
        REQUIRE(cameraInfos.empty());
    }

    SECTION("GetFrameInfo for non-existent returns false") {
        bool bLive    = false;
        int64_t index = 0, pts = 0, frameSize = 0;
        std::string url;
        REQUIRE_FALSE(channelMng.GetFrameInfo("no_ch", bLive, index, pts, frameSize, url));
    }

    SECTION("DeleteInst with nullptr returns false") {
        REQUIRE_FALSE(channelMng.DeleteInst(nullptr, "task1"));
    }
}
