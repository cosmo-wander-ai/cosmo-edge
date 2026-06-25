#include "catch_amalgamated.hpp"
// Unit tests for ActionServiceImpl — video/pic algorithm orchestration management.
// Tests exercise the in-memory CRUD operations without external dependencies.

#include "mock/MockServiceRegistry.h"
#include "service/algorithm/impl/ActionServiceImpl.h"

using namespace cosmo::service;

namespace {

// Helper: create a minimal ActionAlg with the given code and version
cosmo::ActionAlg MakeAlg(const std::string& code, const std::string& version,
                         const std::string& name = "test-alg") {
    cosmo::ActionAlg alg;
    alg.algorithmCode       = code;
    alg.algorithmName       = name;
    alg.algorithmUpdateTime = version;
    return alg;
}

}  // namespace

TEST_CASE("ActionServiceImpl: Video alg CRUD", "[action-service]") {
    cosmo::test::MockServiceRegistry mocks;
    ActionServiceImpl sut;

    SECTION("GetActionAlg returns nullptr for empty code") {
        auto result = sut.GetActionAlg("", "v1");
        REQUIRE(result == nullptr);
    }

    SECTION("GetActionAlg returns nullptr for unknown code") {
        auto result = sut.GetActionAlg("nonexistent", "v1");
        REQUIRE(result == nullptr);
    }

    SECTION("UpdateActionAlg stores and retrieves by code+version") {
        auto alg = MakeAlg("detect_001", "v1");
        REQUIRE(sut.UpdateActionAlg(alg));

        auto retrieved = sut.GetActionAlg("detect_001", "v1");
        REQUIRE(retrieved != nullptr);
        REQUIRE(retrieved->algorithmCode == "detect_001");
        REQUIRE(retrieved->algorithmName == "test-alg");
    }

    SECTION("UpdateActionAlg replaces old version with new version") {
        auto algV1 = MakeAlg("detect_001", "v1");
        REQUIRE(sut.UpdateActionAlg(algV1));

        auto algV2 = MakeAlg("detect_001", "v2", "updated-alg");
        REQUIRE(sut.UpdateActionAlg(algV2));

        // Old version should be gone
        auto oldResult = sut.GetActionAlg("detect_001", "v1");
        REQUIRE(oldResult == nullptr);

        // New version should exist
        auto newResult = sut.GetActionAlg("detect_001", "v2");
        REQUIRE(newResult != nullptr);
        REQUIRE(newResult->algorithmName == "updated-alg");
    }

    SECTION("GetActionAlgByCode returns nullptr for empty code") {
        auto result = sut.GetActionAlgByCode("");
        REQUIRE(result == nullptr);
    }

    SECTION("GetActionAlgByCode finds algorithm by code") {
        auto alg = MakeAlg("classify_002", "v1");
        sut.UpdateActionAlg(alg);

        auto result = sut.GetActionAlgByCode("classify_002");
        REQUIRE(result != nullptr);
        REQUIRE(result->algorithmCode == "classify_002");
    }

    SECTION("UpdateActionAlg with empty JSON string fails") {
        std::string emptyJson;
        REQUIRE_FALSE(sut.UpdateActionAlg(emptyJson));
    }

    SECTION("UpdateActionAlg with invalid JSON fails") {
        std::string badJson = "not-valid-json{{{";
        REQUIRE_FALSE(sut.UpdateActionAlg(badJson));
    }
}

TEST_CASE("ActionServiceImpl: Pic alg CRUD", "[action-service]") {
    cosmo::test::MockServiceRegistry mocks;
    ActionServiceImpl sut;

    SECTION("GetPicActionAlg returns nullptr for empty code") {
        auto result = sut.GetPicActionAlg("", "v1");
        REQUIRE(result == nullptr);
    }

    SECTION("UpdatePicActionAlg stores and retrieves by code+version") {
        auto alg = MakeAlg("pic_detect_001", "v1");
        REQUIRE(sut.UpdatePicActionAlg(alg));

        auto retrieved = sut.GetPicActionAlg("pic_detect_001", "v1");
        REQUIRE(retrieved != nullptr);
        REQUIRE(retrieved->algorithmCode == "pic_detect_001");
    }

    SECTION("GetPicActionAlgByCode returns nullptr for empty code") {
        auto result = sut.GetPicActionAlgByCode("");
        REQUIRE(result == nullptr);
    }

    SECTION("GetPicActionAlgByCode finds algorithm by code") {
        auto alg = MakeAlg("pic_classify", "v1");
        sut.UpdatePicActionAlg(alg);

        auto result = sut.GetPicActionAlgByCode("pic_classify");
        REQUIRE(result != nullptr);
        REQUIRE(result->algorithmCode == "pic_classify");
    }

    SECTION("UpdatePicActionAlg replaces old version") {
        auto algV1 = MakeAlg("pic_algo", "v1");
        sut.UpdatePicActionAlg(algV1);

        auto algV2 = MakeAlg("pic_algo", "v2", "updated-pic");
        sut.UpdatePicActionAlg(algV2);

        REQUIRE(sut.GetPicActionAlg("pic_algo", "v1") == nullptr);
        auto newResult = sut.GetPicActionAlg("pic_algo", "v2");
        REQUIRE(newResult != nullptr);
        REQUIRE(newResult->algorithmName == "updated-pic");
    }

    SECTION("UpdatePicActionAlg with empty JSON string fails") {
        std::string emptyJson;
        REQUIRE_FALSE(sut.UpdatePicActionAlg(emptyJson));
    }
}

TEST_CASE("ActionServiceImpl: Multiple algorithms coexist", "[action-service]") {
    cosmo::test::MockServiceRegistry mocks;
    ActionServiceImpl sut;

    // Store multiple video algorithms
    auto alg1 = MakeAlg("detect_A", "v1", "Detector A");
    auto alg2 = MakeAlg("classify_B", "v1", "Classifier B");
    sut.UpdateActionAlg(alg1);
    sut.UpdateActionAlg(alg2);

    REQUIRE(sut.GetActionAlgByCode("detect_A") != nullptr);
    REQUIRE(sut.GetActionAlgByCode("classify_B") != nullptr);
    REQUIRE(sut.GetActionAlgByCode("detect_A")->algorithmName == "Detector A");
    REQUIRE(sut.GetActionAlgByCode("classify_B")->algorithmName == "Classifier B");
}
