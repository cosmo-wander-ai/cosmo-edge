#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
// Unit tests for LinkageServiceImpl — validates CRUD operations,
// strategy workflow parsing, and query/pagination/filtering.
// The service reads config from disk on construction; we use temp dirs for isolation.

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "service/infra/impl/LinkageServiceImpl.h"
#include "support/ScopedPathOverride.h"

using cosmo::service::LinkageServiceImpl;
using cosmo::util::ErrorEnum;

namespace {

std::string MakeValidWorkflow(const std::string& audio_device_id = "speaker-1",
                              const std::string& audio_file_id   = "audio-1") {
    nlohmann::json alarm;
    alarm["actionId"]               = "LA_AlarmData_Code";
    alarm["actionName"]             = "alarm";
    alarm["flowActionId"]           = "alarm-node";
    alarm["preFlowActionId"]        = "-1";
    alarm["configObject"]["params"] = nlohmann::json::array(
        {{{"key", "algs"}, {"value", R"([{"channelId":"channel-1","algorithmId":"algorithm-1"}])"}}});

    nlohmann::json audio;
    audio["actionId"]        = "LA_AudioDevice_Code";
    audio["actionName"]      = "audio";
    audio["flowActionId"]    = "audio-node";
    audio["preFlowActionId"] = "alarm-node";
    audio["configObject"]["params"] =
        nlohmann::json::array({{{"key", "audioDeviceId"}, {"value", audio_device_id}},
                               {{"key", "operation"}, {"value", "1"}},
                               {{"key", "data"}, {"value", audio_file_id}},
                               {{"key", "text"}, {"value", ""}},
                               {{"key", "volume"}, {"value", "50"}},
                               {{"key", "duration"}, {"value", "60"}},
                               {{"key", "times"}, {"value", "1"}},
                               {{"key", "gap"}, {"value", "1"}}});

    return nlohmann::json::array({std::move(alarm), std::move(audio)}).dump();
}

std::string MakeLegacyWorkflow(const std::string& audio_device_id = "legacy-speaker",
                               const std::string& audio_file_id   = "legacy-audio") {
    auto workflow           = nlohmann::json::parse(MakeValidWorkflow(audio_device_id, audio_file_id));
    workflow[0]["actionId"] = "EVT_00001";
    workflow[0]["configObject"]["params"][0]["key"] = "strageAlgorithms";
    workflow[1]["actionId"]                         = "DA_00001";
    workflow[1]["configObject"]["params"][0]["key"] = "deviceSN";
    workflow[1]["configObject"]["params"][3]["key"] = "dataText";
    return workflow.dump();
}

std::string MakeLegacyTextWorkflow() {
    auto workflow = nlohmann::json::parse(MakeLegacyWorkflow("legacy-text-speaker", "unused-audio"));
    workflow[1]["configObject"]["params"][1]["value"] = "2";
    workflow[1]["configObject"]["params"][3]["value"] = "legacy text";
    return workflow.dump();
}

// Helper to create a temp test directory with required structure
struct LinkageTestEnv {
    std::string baseDir;
    cosmo::test::ScopedPathOverride pathOverride;

    LinkageTestEnv()
        : baseDir(std::string("/tmp/cosmo_linkage_") + std::to_string(getpid())),
          pathOverride(baseDir, baseDir) {
        std::filesystem::remove_all(baseDir);
        std::filesystem::create_directories(baseDir + "/conf/linkAge");
    }

    ~LinkageTestEnv() {
        std::error_code error;
        std::filesystem::remove_all(baseDir, error);
    }
};

}  // namespace

TEST_CASE("LinkageServiceImpl: CRUD and query operations", "[linkage-service]") {
    LinkageTestEnv env;

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

    SECTION("Add creates a draft for an empty workflow") {
        std::string id;
        auto ret = sut.Add("test_strategy", "[]", id);
        REQUIRE(ret == ErrorEnum::Success);
        REQUIRE_FALSE(id.empty());

        size_t total       = 0;
        const auto results = sut.Query(1, 10, "test_strategy", total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results.front().id == id);
        REQUIRE(nlohmann::json::parse(results.front().workFlow).empty());
    }

    SECTION("Update still rejects an empty workflow") {
        std::string id;
        REQUIRE(sut.Add("configured", MakeValidWorkflow(), id) == ErrorEnum::Success);
        REQUIRE(sut.Update("draft", id, "[]") == ErrorEnum::ParameterException);
    }

    SECTION("Add then Delete succeeds") {
        std::string id;
        auto ret = sut.Add("to_delete", MakeValidWorkflow(), id);
        REQUIRE(ret == ErrorEnum::Success);

        ret = sut.Delete(id);
        REQUIRE(ret == ErrorEnum::Success);

        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(total == 0);
    }

    SECTION("Update non-existent ID returns StrategyNotExist") {
        auto ret = sut.Update("name", "fake-id", MakeValidWorkflow());
        REQUIRE(ret == ErrorEnum::StrategyNotExist);
    }

    SECTION("Add then Update succeeds") {
        std::string id;
        sut.Add("original", MakeValidWorkflow(), id);

        auto ret = sut.Update("updated_name", id, MakeValidWorkflow("speaker-2", "audio-2"));
        REQUIRE(ret == ErrorEnum::Success);

        size_t total = 0;
        auto results = sut.Query(1, 10, "", total);
        REQUIRE(total == 1);
        REQUIRE(results[0].name == "updated_name");
    }

    SECTION("Switch changes strategy status") {
        std::string id;
        sut.Add("switchable", MakeValidWorkflow(), id);

        auto ret = sut.Switch(id, false);
        REQUIRE(ret == ErrorEnum::Success);

        ret = sut.Switch(id, true);
        REQUIRE(ret == ErrorEnum::Success);
    }

    SECTION("Query with name filter") {
        std::string id1, id2;
        sut.Add("alpha_strategy", MakeValidWorkflow(), id1);
        sut.Add("beta_strategy", MakeValidWorkflow(), id2);

        size_t total = 0;
        auto results = sut.Query(1, 10, "alpha", total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].name == "alpha_strategy");
    }

    SECTION("Query with pagination") {
        std::string id;
        sut.Add("s1", MakeValidWorkflow(), id);
        sut.Add("s2", MakeValidWorkflow(), id);
        sut.Add("s3", MakeValidWorkflow(), id);

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

    SECTION("Resource-defined action and parameter IDs bind to runtime tasks") {
        std::string id;
        REQUIRE(sut.Add("bound", MakeValidWorkflow("speaker-42", "audio-42"), id) == ErrorEnum::Success);
        REQUIRE(sut.IsAudioDeviceInUse("speaker-42"));
        REQUIRE(sut.IsAudioFileInUse("audio-42"));
    }

    SECTION("Legacy action and parameter IDs validate, persist, and bind to runtime tasks") {
        std::string id;
        REQUIRE(sut.Add("legacy", MakeLegacyWorkflow(), id) == ErrorEnum::Success);
        REQUIRE(sut.IsAudioDeviceInUse("legacy-speaker"));
        REQUIRE(sut.IsAudioFileInUse("legacy-audio"));

        size_t total       = 0;
        const auto results = sut.Query(1, 10, "legacy", total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        const auto persisted_workflow = nlohmann::json::parse(results.front().workFlow);
        REQUIRE(persisted_workflow[0]["actionId"] == "EVT_00001");
        REQUIRE(persisted_workflow[1]["actionId"] == "DA_00001");
    }

    SECTION("Legacy text parameters validate and bind to a runtime task") {
        std::string id;
        REQUIRE(sut.Add("legacy-text", MakeLegacyTextWorkflow(), id) == ErrorEnum::Success);
        REQUIRE(sut.IsAudioDeviceInUse("legacy-text-speaker"));
        REQUIRE_FALSE(sut.IsAudioFileInUse("unused-audio"));
    }

    SECTION("Validation keeps the first matching compatibility parameter") {
        auto workflow = nlohmann::json::parse(MakeValidWorkflow());
        workflow[1]["configObject"]["params"].push_back({{"key", "deviceSN"}, {"value", ""}});
        std::string id;
        REQUIRE(sut.Add("first-match", workflow.dump(), id) == ErrorEnum::Success);
        REQUIRE_FALSE(id.empty());
        REQUIRE_FALSE(sut.IsAudioDeviceInUse("speaker-1"));
    }

    SECTION("Unknown actions are rejected without persistence") {
        auto workflow           = nlohmann::json::parse(MakeValidWorkflow());
        workflow[1]["actionId"] = "unsupported-action";
        std::string id;
        REQUIRE(sut.Add("unsupported", workflow.dump(), id) == ErrorEnum::ParameterException);
        REQUIRE(id.empty());
        size_t total = 0;
        REQUIRE(sut.Query(1, 10, "unsupported", total).empty());
        REQUIRE(total == 0);
    }

    SECTION("Dangling workflow nodes are rejected without persistence") {
        auto workflow                  = nlohmann::json::parse(MakeValidWorkflow());
        workflow[1]["preFlowActionId"] = "missing-parent";
        std::string id;
        REQUIRE(sut.Add("dangling", workflow.dump(), id) == ErrorEnum::ParameterException);
        size_t total = 0;
        REQUIRE(sut.Query(1, 10, "", total).empty());
        REQUIRE(total == 0);
    }

    SECTION("ReadSupportedStorage returns non-negative total") {
        int totalSize = 0;
        std::vector<cosmo::StorageList> storages;
        bool result = sut.ReadSupportedStorage(totalSize, storages);
        REQUIRE(result == true);
        REQUIRE(totalSize >= 0);
    }
}

TEST_CASE("LinkageServiceImpl: empty workflow draft survives reload", "[linkage-service]") {
    LinkageTestEnv env;

    std::string id;
    {
        LinkageServiceImpl writer;
        REQUIRE(writer.Add("draft", "[]", id) == ErrorEnum::Success);
    }

    LinkageServiceImpl reader;
    size_t total       = 0;
    const auto results = reader.Query(1, 10, "draft", total);
    REQUIRE(total == 1);
    REQUIRE(results.size() == 1);
    REQUIRE(results.front().id == id);
    REQUIRE(nlohmann::json::parse(results.front().workFlow).empty());
}

TEST_CASE("LinkageServiceImpl: persistence failure does not publish a ghost strategy",
          "[linkage-service][consistency]") {
    LinkageTestEnv env;

    const auto config_path = std::filesystem::path(env.baseDir) / "conf/linkAge/linkAgeList.json";
    REQUIRE(std::filesystem::create_directory(config_path));

    LinkageServiceImpl sut;
    std::string id;
    REQUIRE(sut.Add("not-persisted", MakeValidWorkflow(), id) == ErrorEnum::Failed);
    REQUIRE(id.empty());
    for (const auto& entry : std::filesystem::directory_iterator(config_path.parent_path())) {
        REQUIRE_FALSE(entry.is_regular_file());
    }

    size_t total = 0;
    REQUIRE(sut.Query(1, 10, "", total).empty());
    REQUIRE(total == 0);
}

TEST_CASE("LinkageServiceImpl: Stop is idempotent and rejects new alarms", "[linkage-service][lifecycle]") {
    LinkageTestEnv env;

    LinkageServiceImpl sut;
    REQUIRE_NOTHROW(sut.Stop());
    REQUIRE_NOTHROW(sut.Stop());
    REQUIRE_FALSE(sut.Alarm("channel", "algorithm"));
}
