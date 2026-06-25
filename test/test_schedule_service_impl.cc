#include <chrono>
#include <filesystem>

#include "catch_amalgamated.hpp"
#include "mock/MockServiceRegistry.h"
#include "service/task/impl/ScheduleServiceImpl.h"
#include "util/PathUtil.h"

using namespace cosmo::service;
using namespace cosmo;

TEST_CASE("ScheduleServiceImpl: 时间模板 CRUD", "[schedule-service]") {
    // 设置测试专用的隔离目录
    std::string testBaseDir =
        "/tmp/cosmo_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);

    // Change working directory so CfgSave writes to test directory
    auto oldPath = std::filesystem::current_path();
    std::filesystem::current_path(testBaseDir);

    cosmo::test::MockServiceRegistry mocks;
    cosmo::path::OverrideRootPathForTest(testBaseDir, testBaseDir);
    ScheduleServiceImpl sut;

    SECTION("Init 能够加载默认的模板") {
        std::string defaultId = sut.GetDefaultId();
        REQUIRE(defaultId == "e89c6c6385e5454b35cde0d1653vg");
        REQUIRE(sut.Exist(defaultId) == true);

        std::string name;
        REQUIRE(sut.Exist(defaultId, name) == true);
        REQUIRE(name == "全天候");
    }

    SECTION("Add 和 Query 方法") {
        MsgScheduleTemplate tpl;
        tpl.scheduleName = "Test Schedule";
        std::string newId;
        REQUIRE(sut.Add(tpl, newId) == cosmo::util::ErrorEnum::Success);
        REQUIRE(!newId.empty());
        REQUIRE(sut.Exist(newId) == true);

        size_t total = 0;
        auto list    = sut.Query("", 1, 10, total);
        // Default configs (3) + newly added (1) = 4
        REQUIRE(total == 4);
        REQUIRE(list.size() == 4);

        SECTION("Update 能够更新用户自定义模板，但不能更新默认模板") {
            MsgScheduleTemplate updateTpl = tpl;
            updateTpl.scheduleName        = "Updated Schedule";
            updateTpl.scheduleId          = newId;
            REQUIRE(sut.Update(updateTpl) == cosmo::util::ErrorEnum::Success);

            std::string defaultId = sut.GetDefaultId();
            MsgScheduleTemplate defTpl;
            defTpl.scheduleId = defaultId;
            REQUIRE(sut.Update(defTpl) == cosmo::util::ErrorEnum::DefaultCantBeUpdate);
        }

        SECTION("Delete 可以删除自定义模板，但不能删除默认模板") {
            REQUIRE(sut.Delete(newId) == cosmo::util::ErrorEnum::Success);
            REQUIRE(sut.Exist(newId) == false);

            std::string defaultId = sut.GetDefaultId();
            REQUIRE(sut.Delete(defaultId) == cosmo::util::ErrorEnum::DefaultCantBeDelete);
        }
    }

    SECTION("InRunTime 判断逻辑") {
        std::string defaultId = sut.GetDefaultId();
        // 全天候模板无论什么时间都应该是 true
        REQUIRE(sut.InRunTime(defaultId) == true);
    }

    // 恢复工作目录
    std::filesystem::current_path(oldPath);
    std::filesystem::remove_all(testBaseDir);
}
