#include "catch_amalgamated.hpp"
/*
 * test_algorithm_service_impl.cc - AlgorithmServiceImpl 核心路径单元测试
 *
 * 测试策略: 不调用 Init()（需要文件系统+DI），
 * 仅测试不依赖外部状态的纯逻辑路径。
 */
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "mock/MockServiceRegistry.h"
#include "service/algorithm/impl/AlgorithmPacketLoader.h"
#include "service/algorithm/impl/AlgorithmServiceImpl.h"
#include "service/detail/ServiceRegistry.h"

using namespace cosmo::service;

// ============================================================
// 构造 / 析构安全
// ============================================================

TEST_CASE("AlgorithmServiceImpl: construction and destruction without Init", "[AlgorithmService]") {
    REQUIRE_NOTHROW([]() { AlgorithmServiceImpl svc; }());
}

// ============================================================
// 查询参数校验
// ============================================================

TEST_CASE("AlgorithmServiceImpl: Query with invalid pagination returns empty", "[AlgorithmService]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;
    size_t total = 0;

    SECTION("pageNum = 0") {
        auto result = svc.Query("", "", "", "", "", 0, 10, total);
        REQUIRE(result.empty());
    }

    SECTION("pageSize = 0") {
        auto result = svc.Query("", "", "", "", "", 1, 0, total);
        REQUIRE(result.empty());
    }

    SECTION("negative pageNum") {
        auto result = svc.Query("", "", "", "", "", -1, 10, total);
        REQUIRE(result.empty());
    }
}

TEST_CASE("AlgorithmServiceImpl: Query with valid pages but no algorithms returns empty",
          "[AlgorithmService]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;
    size_t total = 0;
    auto result  = svc.Query("", "", "", "", "", 1, 10, total);
    REQUIRE(result.empty());
    REQUIRE(total == 0);
}

// ============================================================
// 非存在算法操作
// ============================================================

TEST_CASE("AlgorithmServiceImpl: operations on non-existent algorithm", "[AlgorithmService]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;

    SECTION("Delete non-existent returns AlgorithmNotExist") {
        auto ret = svc.Delete("alg_not_exist");
        REQUIRE(ret == cosmo::util::ErrorEnum::AlgorithmNotExist);
    }

    SECTION("Update non-existent returns AlgorithmNotExist") {
        auto ret = svc.Update("alg_not_exist", "name", 0, "remark");
        REQUIRE(ret == cosmo::util::ErrorEnum::AlgorithmNotExist);
    }

    SECTION("GetAlgorithmName for non-existent returns empty") {
        auto name = svc.GetAlgorithmName("alg_not_exist");
        REQUIRE(name.empty());
    }

    SECTION("GetMetaData for non-existent returns empty") {
        auto meta = svc.GetMetaData("alg_not_exist");
        REQUIRE(meta.empty());
    }

    SECTION("GetAlgorithmsByModelId for non-existent returns empty") {
        auto algs = svc.GetAlgorithmsByModelId("model_not_exist");
        REQUIRE(algs.empty());
    }
}

// ============================================================
// Add(AlgorithmPacketInfo) 直接写入并查询
// ============================================================

TEST_CASE("AlgorithmServiceImpl: Add and Query packet info", "[AlgorithmService]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;

    cosmo::service::algorithm::AlgorithmPacketInfo packet;
    packet.id                = "test_alg_001";
    packet.algorithmCode     = 1001;
    packet.algorithmName     = "TestDetect";
    packet.algorithmCategory = 1;
    packet.algorithmUsage    = 1;
    packet.remark            = "test remark";
    packet.supplier          = "TEST";
    packet.updateTime        = 100;

    auto addRet = svc.Add(packet);
    REQUIRE(addRet == cosmo::util::ErrorEnum::Success);

    SECTION("Query returns the added algorithm") {
        size_t total = 0;
        auto results = svc.Query("", "", "", "", "", 1, 10, total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].id == "test_alg_001");
        REQUIRE(results[0].algorithmName == "TestDetect");
    }

    SECTION("GetAlgorithmName returns correct name") {
        REQUIRE(svc.GetAlgorithmName("test_alg_001") == "TestDetect");
    }

    SECTION("Query with name filter works") {
        size_t total = 0;
        auto results = svc.Query("", "NoMatch", "", "", "", 1, 10, total);
        REQUIRE(results.empty());
        REQUIRE(total == 0);
    }

    SECTION("Query with fuzzy name filter works") {
        size_t total = 0;
        auto results = svc.Query("", "Detect", "", "", "", 1, 10, total);
        REQUIRE(results.size() == 1);
    }

    SECTION("Delete the added algorithm succeeds") {
        // Delete accesses file system for path - but the algorithm has empty filePath
        // so RemovePath("") is called, which is safe
        auto ret = svc.Delete("test_alg_001");
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);

        REQUIRE(svc.GetAlgorithmName("test_alg_001").empty());
    }

    SECTION("Add duplicate replaces existing") {
        cosmo::service::algorithm::AlgorithmPacketInfo packet2;
        packet2.id             = "test_alg_001";
        packet2.algorithmCode  = 1001;
        packet2.algorithmName  = "TestDetectV2";
        packet2.algorithmUsage = 1;
        packet2.updateTime     = 200;

        auto ret = svc.Add(packet2);
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);

        REQUIRE(svc.GetAlgorithmName("test_alg_001") == "TestDetectV2");

        size_t total = 0;
        auto results = svc.Query("", "", "", "", "", 1, 100, total);
        REQUIRE(total == 1);  // 不是2，因为是覆盖
    }

    SECTION("GetAlgorithmsByModelId returns matching algorithm") {
        // mock a model association
        // AlgorithmPacketInfo has modelInfo, but it seems we need to populate it.
        // It's a complex struct. But we can just see if we can set it and test it.
        cosmo::service::algorithm::AlgorithmPacketInfo p;
        p.id            = "alg_model_001";
        p.algorithmCode = 9999;
        p.algorithmName = "AlgWithModel";
        p.updateTime    = 300;

        cosmo::service::algorithm::AlgorithmModelInfo m;
        m.modelCode = "test_model_001";
        p.modelInfo.models.push_back(m);

        svc.Add(p);

        auto algs = svc.GetAlgorithmsByModelId("test_model_001");
        REQUIRE(algs.size() == 1);
        REQUIRE(algs[0] == "alg_model_001");
    }

    SECTION("GetMetaData returns JSON content") {
        cosmo::service::algorithm::AlgorithmPacketInfo p;
        p.id            = "alg_meta_001";
        p.algorithmCode = 8888;
        p.algorithmName = "AlgWithMeta";
        p.updateTime    = 400;

        p.algorithmMetadata = "{\"test_key\":\"test_value\"}";

        svc.Add(p);

        auto meta = svc.GetMetaData("alg_meta_001");
        REQUIRE(!meta.empty());
        REQUIRE(meta.find("test_key") != std::string::npos);
        REQUIRE(meta.find("test_value") != std::string::npos);
    }
}

TEST_CASE("AlgorithmServiceImpl: duplicate Add keeps replacement file at the same path",
          "[AlgorithmService]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;

    auto dir = std::filesystem::temp_directory_path() / "cosmo_algorithm_duplicate_add_test";
    std::filesystem::create_directories(dir);
    auto filePath = dir / "1001_TestDetect_20260513.json";
    {
        std::ofstream out(filePath);
        out << R"({"algorithmId":"1001","algorithmName":"TestDetect"})";
    }

    cosmo::service::algorithm::AlgorithmPacketInfo packet;
    packet.id            = "1001";
    packet.algorithmCode = 1001;
    packet.algorithmName = "TestDetect";
    packet.filePath      = filePath.string();
    REQUIRE(svc.Add(packet) == cosmo::util::ErrorEnum::Success);

    packet.algorithmName = "TestDetectV2";
    REQUIRE(svc.Add(packet) == cosmo::util::ErrorEnum::Success);

    REQUIRE(std::filesystem::exists(filePath));
    REQUIRE(svc.GetAlgorithmName("1001") == "TestDetectV2");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("AlgorithmPacketLoader: LoadFromJsonDirectory scans nested folders",
          "[AlgorithmService][AlgorithmPacketLoader]") {
    auto dir       = std::filesystem::temp_directory_path() / "cosmo_algorithm_nested_json_test";
    auto nestedDir = dir / "outer" / "inner";
    std::filesystem::create_directories(nestedDir);
    auto filePath = nestedDir / "1002_NestedDetect_20260513.json";
    {
        std::ofstream out(filePath);
        out << R"({"algorithmId":"1002","algorithmName":"NestedDetect","algorithmUsage":1})";
    }

    auto packets = cosmo::service::detail::AlgorithmPacketLoader::LoadFromJsonDirectory(dir.string());

    REQUIRE(packets.size() == 1);
    REQUIRE(packets[0].id == "1002");
    REQUIRE(packets[0].algorithmName == "NestedDetect");
    REQUIRE(packets[0].filePath == filePath.string());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ============================================================
// Update 已存在的算法
// ============================================================

TEST_CASE("AlgorithmServiceImpl: Update existing algorithm name", "[AlgorithmService]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;

    cosmo::service::algorithm::AlgorithmPacketInfo packet;
    packet.id             = "upd_alg_001";
    packet.algorithmCode  = 2001;
    packet.algorithmName  = "OldName";
    packet.algorithmUsage = 0;
    packet.updateTime     = 100;
    svc.Add(packet);

    auto ret = svc.Update("upd_alg_001", "NewName", 2, "new remark");
    REQUIRE(ret == cosmo::util::ErrorEnum::Success);

    REQUIRE(svc.GetAlgorithmName("upd_alg_001") == "NewName");
}

// ============================================================
// 分页测试
// ============================================================

TEST_CASE("AlgorithmServiceImpl: Query pagination", "[AlgorithmService]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;

    // 插入 5 个算法
    for (int i = 0; i < 5; i++) {
        cosmo::service::algorithm::AlgorithmPacketInfo packet;
        packet.id             = "pag_" + std::to_string(i);
        packet.algorithmCode  = 3000 + i;
        packet.algorithmName  = "Alg_" + std::to_string(i);
        packet.algorithmUsage = 0;
        packet.updateTime     = 100 + i;
        svc.Add(packet);
    }

    size_t total = 0;

    SECTION("Page 1 of 2 returns 2 items") {
        auto results = svc.Query("", "", "", "", "", 1, 2, total);
        REQUIRE(total == 5);
        REQUIRE(results.size() == 2);
    }

    SECTION("Page 2 of 2 returns 2 items") {
        auto results = svc.Query("", "", "", "", "", 2, 2, total);
        REQUIRE(total == 5);
        REQUIRE(results.size() == 2);
    }

    SECTION("Page 3 of 2 returns 1 item") {
        auto results = svc.Query("", "", "", "", "", 3, 2, total);
        REQUIRE(total == 5);
        REQUIRE(results.size() == 1);
    }

    SECTION("Page beyond data returns empty") {
        auto results = svc.Query("", "", "", "", "", 10, 2, total);
        REQUIRE(total == 5);
        REQUIRE(results.empty());
    }
}

// ============================================================
// 并发安全
// ============================================================

TEST_CASE("AlgorithmServiceImpl: concurrent Query and Add are safe", "[AlgorithmService][concurrency]") {
    cosmo::test::MockServiceRegistry mocks;
    AlgorithmServiceImpl svc;

    std::atomic<bool> stop{false};
    std::atomic<int> opCount{0};

    // 读线程
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; i++) {
        threads.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                size_t total = 0;
                auto r       = svc.Query("", "", "", "", "", 1, 10, total);
                (void)r;
                opCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 写线程
    threads.emplace_back([&]() {
        for (int i = 0; i < 50 && !stop.load(std::memory_order_relaxed); i++) {
            cosmo::service::algorithm::AlgorithmPacketInfo p;
            p.id             = "conc_" + std::to_string(i);
            p.algorithmCode  = 5000 + i;
            p.algorithmName  = "ConcAlg_" + std::to_string(i);
            p.algorithmUsage = 0;
            p.updateTime     = i;
            svc.Add(p);
            opCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(opCount.load() > 0);
}
