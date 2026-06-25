#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
/*
 * test_db_service_impl.cc — DbServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: use OverrideRootPathForTest to redirect path to temp directory,
 * verify SQLite database creation and basic operations.
 */
#include <SQLiteCpp/SQLiteCpp.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

#include "mock/MockServiceRegistry.h"
#include "service/infra/impl/DbServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("DbServiceImpl: construction creates DB and GetDb returns valid ptr", "[DbService]") {
    std::string testDir =
        "/tmp/cosmo_db_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testDir);

    cosmo::test::MockServiceRegistry mocks;
    cosmo::path::OverrideRootPathForTest(testDir, testDir);

    DbServiceImpl sut;

    SECTION("GetDb returns non-null shared_ptr") {
        auto db = sut.GetDb();
        REQUIRE(db != nullptr);
    }

    SECTION("Multiple GetDb calls return same instance") {
        auto db1 = sut.GetDb();
        auto db2 = sut.GetDb();
        REQUIRE(db1.get() == db2.get());
    }

    SECTION("DB file is created on disk") {
        auto dbFile = testDir + "/db/ied.db";
        REQUIRE(std::filesystem::exists(dbFile));
    }

    SECTION("DB supports basic SQL operations") {
        auto db = sut.GetDb();
        REQUIRE_NOTHROW(db->exec("CREATE TABLE IF NOT EXISTS test_tbl (id INTEGER PRIMARY KEY, val TEXT)"));
        REQUIRE_NOTHROW(db->exec("INSERT INTO test_tbl (val) VALUES ('hello')"));
    }

    std::filesystem::remove_all(testDir);
}

TEST_CASE("DbServiceImpl: concurrent GetDb access", "[DbService]") {
    std::string testDir =
        "/tmp/cosmo_db_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testDir);

    cosmo::test::MockServiceRegistry mocks;
    cosmo::path::OverrideRootPathForTest(testDir, testDir);

    DbServiceImpl sut;

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            auto db = sut.GetDb();
            if (db != nullptr) {
                successCount.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(successCount.load() == 4);
    std::filesystem::remove_all(testDir);
}

TEST_CASE("DbServiceImpl: Init creates required directories", "[DbService]") {
    std::string testDir =
        "/tmp/cosmo_db_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testDir);

    cosmo::test::MockServiceRegistry mocks;
    cosmo::path::OverrideRootPathForTest(testDir, testDir);

    DbServiceImpl sut;
    sut.Init();

    auto dbPath = cosmo::path::GetDbPath();
    REQUIRE(std::filesystem::exists(dbPath));

    std::filesystem::remove_all(testDir);
}
