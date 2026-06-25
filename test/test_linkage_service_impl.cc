#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
// Unit tests for LinkageServiceImpl — validates CRUD operations,
// strategy workflow parsing, and query/pagination/filtering.
// The service reads config from disk on construction; we use temp dirs for isolation.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mock/MockServiceRegistry.h"
#include "service/detail/ServiceRegistry.h"
#include "service/infra/impl/LinkageServiceImpl.h"

using cosmo::service::LinkageServiceImpl;
using cosmo::util::ErrorEnum;

namespace {

// Helper to create a temp test directory with required structure
struct LinkageTestEnv {
    std::string baseDir;

    LinkageTestEnv() {
        baseDir = std::string("/tmp/cosmo_linkage_") + std::to_string(getpid());
        // NOTE: OverrideRootPathForTest is called AFTER MockServiceRegistry construction
        // to avoid being overwritten by MockServiceRegistry's default path setup.
    }

    void SetupPaths() {
        cosmo::path::OverrideRootPathForTest(baseDir, baseDir);
        auto cfgDir = baseDir + "/conf/linkAge";
        mkdir((baseDir + "/conf").c_str(), 0777);
        mkdir(cfgDir.c_str(), 0777);
    }

    ~LinkageTestEnv() {
        std::string cmd = "rm -rf " + baseDir;
        if (system(cmd.c_str())) {
        }
    }
};

}  // namespace

TEST_CASE("LinkageServiceImpl: CRUD and query operations", "[linkage-service]") {
    LinkageTestEnv env;

    cosmo::test::MockServiceRegistry mocks;
    // Must call SetupPaths AFTER MockServiceRegistry to override its default paths
    env.SetupPaths();

    LinkageServiceImpl sut;

    SECTION("Query returns empty list when no strategies exist") {
        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(results.empty());
        REQUIRE(total == 0);
    }

    SECTION("Delete non-existent ID returns IDNotExist") {
        std::string fakeId = "non-existent-id";
        auto ret           = sut.Delete(fakeId);
        REQUIRE(ret == ErrorEnum::IDNotExist);
    }

    SECTION("Switch non-existent ID returns IDNotExist") {
        std::string fakeId = "non-existent-id";
        auto ret           = sut.Switch(fakeId, true);
        REQUIRE(ret == ErrorEnum::IDNotExist);
    }

    SECTION("Add with invalid workflow JSON fails") {
        std::string id;
        auto ret = sut.Add("test_strategy", "not valid json{{{", id);
        REQUIRE(ret == ErrorEnum::Failed);
    }

    SECTION("Add with valid empty workflow succeeds") {
        std::string id;
        auto ret = sut.Add("test_strategy", "[]", id);
        REQUIRE(ret == ErrorEnum::Success);
        REQUIRE_FALSE(id.empty());

        // Verify it appears in Query
        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].name == "test_strategy");
    }

    SECTION("Add then Delete succeeds") {
        std::string id;
        auto ret = sut.Add("to_delete", "[]", id);
        REQUIRE(ret == ErrorEnum::Success);

        ret = sut.Delete(id);
        REQUIRE(ret == ErrorEnum::Success);

        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(total == 0);
    }

    SECTION("Update non-existent ID returns StrategyNotExist") {
        auto ret = sut.Update("name", "fake-id", "[]");
        REQUIRE(ret == ErrorEnum::StrategyNotExist);
    }

    SECTION("Add then Update succeeds") {
        std::string id;
        sut.Add("original", "[]", id);

        auto ret = sut.Update("updated_name", id, "[]");
        REQUIRE(ret == ErrorEnum::Success);

        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(total == 1);
        REQUIRE(results[0].name == "updated_name");
    }

    SECTION("Switch changes strategy status") {
        std::string id;
        sut.Add("switchable", "[]", id);

        auto ret = sut.Switch(id, false);
        REQUIRE(ret == ErrorEnum::Success);

        ret = sut.Switch(id, true);
        REQUIRE(ret == ErrorEnum::Success);
    }

    SECTION("Query with name filter") {
        std::string id1, id2;
        sut.Add("alpha_strategy", "[]", id1);
        sut.Add("beta_strategy", "[]", id2);

        size_t total = 0;
        auto results = sut.Query(1, 10, "alpha", total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].name == "alpha_strategy");
    }

    SECTION("Query with pagination") {
        std::string id;
        sut.Add("s1", "[]", id);
        sut.Add("s2", "[]", id);
        sut.Add("s3", "[]", id);

        size_t total = 0;
        auto page1   = sut.Query(1, 2, "", total);
        REQUIRE(total == 3);
        REQUIRE(page1.size() == 2);

        total      = 0;
        auto page2 = sut.Query(2, 2, "", total);
        REQUIRE(total == 3);
        REQUIRE(page2.size() == 1);
    }

    SECTION("Alarm enqueue does not crash") {
        REQUIRE(sut.Alarm("ch1", "alg1") == true);
    }

    SECTION("IsAudioDeviceInUse returns false when no strategies") {
        REQUIRE(sut.IsAudioDeviceInUse("dev1") == false);
    }

    SECTION("IsAudioFileInUse returns false when no strategies") {
        REQUIRE(sut.IsAudioFileInUse("file1") == false);
    }

    SECTION("ReadSupportedStorage returns non-negative total") {
        int totalSize = 0;
        std::vector<cosmo::StorageList> storages;
        bool result = sut.ReadSupportedStorage(totalSize, storages);
        REQUIRE(result == true);
        REQUIRE(totalSize >= 0);
    }
}
