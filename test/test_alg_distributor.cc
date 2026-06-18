#include "catch_amalgamated.hpp"
/*
 * test_alg_distributor.cc - AlgDataQueueDistributor 单元测试
 */
#include "flow/common/AlgDataQueueDistributor.h"

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
