#include "catch_amalgamated.hpp"
/*
 * test_alg_channel_mng.cc - AlgChannelMng 单元测试
 */
#include "flow/channel/AlgChannelMng.h"

using namespace cosmo;

TEST_CASE("AlgChannelMng initial state", "[AlgChannelMng]") {
    AlgChannelMng channelMng;

    SECTION("DeleteInst with nullptr returns false") {
        REQUIRE_FALSE(channelMng.DeleteInst(nullptr, "task1"));
    }
}
