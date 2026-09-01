#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "catch_amalgamated.hpp"
#include "mock/MockAlgorithmService.h"
#include "mock/MockServiceRegistry.h"
#include "nlohmann/json.hpp"
#include "service/algorithm/impl/AlgorithmServiceImpl.h"
#include "util/JsonFileUtil.h"
#include "util/PathUtil.h"

namespace {

namespace fs = std::filesystem;

struct ManagedAlgorithmPathFixture {
    cosmo::test::MockServiceRegistry mocks;
    fs::path root;
    fs::path data_root;
    fs::path app_root;
    fs::path outside_root;

    ManagedAlgorithmPathFixture() {
        root = fs::temp_directory_path() /
               ("cosmo_algorithm_managed_paths_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        data_root    = root / "data";
        app_root     = root / "app";
        outside_root = root / "outside";
        fs::create_directories(data_root);
        fs::create_directories(app_root);
        fs::create_directories(outside_root);
        cosmo::path::OverrideRootPathForTest(data_root.string(), app_root.string());
        fs::create_directories(cosmo::path::GetAlgorithmPath());
        fs::create_directories(fs::path(cosmo::path::GetResourcePath()) / "algorithm_template");
        fs::create_directories(cosmo::path::GetLayoutPath());
    }

    ~ManagedAlgorithmPathFixture() {
        cosmo::path::OverrideRootPathForTest("/data/cwaiuserdata", "/appfs/cosmo_wander/cwai_data");
        std::error_code error;
        fs::remove_all(root, error);
    }

    fs::path AlgorithmRoot() const {
        return app_root / "resource" / "algorithm";
    }

    fs::path TemplateRoot() const {
        return app_root / "resource" / "algorithm_template";
    }

    fs::path LayoutRoot() const {
        return app_root / "resource" / "layout";
    }
};

void WriteJson(const fs::path& path, const std::string& json) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    REQUIRE(stream.is_open());
    stream << json;
    REQUIRE(stream.good());
}

size_t CountRegularFiles(const fs::path& directory) {
    size_t count = 0;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (entry.is_regular_file(error)) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST_CASE("AlgorithmServiceImpl confines JSON creation to the managed algorithm root",
          "[AlgorithmService][security][path]") {
    ManagedAlgorithmPathFixture fix;
    cosmo::service::AlgorithmServiceImpl service;
    const auto attacker_directory = fix.outside_root / "attacker-selected";
    fs::create_directories(attacker_directory);

    REQUIRE(service.AddFromJson("ManagedAlgorithm", 1, 1, "remark", "event", attacker_directory.string()) ==
            cosmo::util::ErrorEnum::Success);
    REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
    REQUIRE(CountRegularFiles(attacker_directory) == 0);

    size_t total          = 0;
    const auto algorithms = service.Query("", "ManagedAlgorithm", "", "", "", 1, 10, total);
    REQUIRE(total == 1);
    REQUIRE(algorithms.size() == 1);
}

TEST_CASE("AlgorithmServiceImpl rejects path components in algorithm names",
          "[AlgorithmService][security][path]") {
    ManagedAlgorithmPathFixture fix;
    cosmo::service::AlgorithmServiceImpl service;

    REQUIRE(service.AddFromJson("../../escaped", 1, 1, "", "", fix.outside_root.string()) ==
            cosmo::util::ErrorEnum::InvalidParam);
    REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 0);
    REQUIRE_FALSE(fs::exists(fix.root / "escaped"));
}

TEST_CASE("Algorithm exports stay within managed source and temporary roots",
          "[AlgorithmService][security][path][export]") {
    ManagedAlgorithmPathFixture fix;
    cosmo::service::AlgorithmServiceImpl service;

    WriteJson(fix.AlgorithmRoot() / "safe_Name_1.json", R"({"algorithmCode":"safe","algorithmName":"Name"})");

    SECTION("single export rejects an unsafe algorithm code") {
        cosmo::service::algorithm::LayoutExportResult result;
        CHECK(service.LayoutExportSingle("../../outside", "Name", "", "", "", result) ==
              cosmo::util::ErrorEnum::InvalidParam);
        CHECK(result.filePath.empty());
    }

    SECTION("single export rejects a symlink source") {
        const auto outside_json = fix.outside_root / "outside.json";
        WriteJson(outside_json, R"({"algorithmCode":"linked","algorithmName":"Name"})");
        fs::create_symlink(outside_json, fix.AlgorithmRoot() / "linked_Name_1.json");

        cosmo::service::algorithm::LayoutExportResult result;
        CHECK(service.LayoutExportSingle("linked", "Name", "", "", "", result) ==
              cosmo::util::ErrorEnum::ParameterException);
        CHECK(result.filePath.empty());
    }

    SECTION("valid single export returns a regular file under the temporary root") {
        cosmo::service::algorithm::LayoutExportResult result;
        REQUIRE(service.LayoutExportSingle("safe", "Name", "", "", "", result) ==
                cosmo::util::ErrorEnum::Success);
        std::string resolved;
        REQUIRE(
            cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetTemporaryDirPath(), result.filePath,
                                                       cosmo::path::PathEntryType::kRegularFile, resolved));
        CHECK(resolved == result.filePath);
        const auto permissions = fs::status(result.filePath).permissions();
        CHECK((permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
        std::error_code error;
        fs::remove(result.filePath, error);
    }

    SECTION("batch export rejects path-like filter identifiers") {
        cosmo::service::algorithm::LayoutExportResult result;
        CHECK(service.LayoutExportAll("", "", "", "", {"../safe"}, result) ==
              cosmo::util::ErrorEnum::InvalidParam);
        CHECK(result.filePath.empty());
    }
}

TEST_CASE("Algorithm layout queries accept only the managed algorithm and template roots",
          "[AlgorithmService][security][path]") {
    ManagedAlgorithmPathFixture fix;
    cosmo::service::AlgorithmServiceImpl service;
    WriteJson(fix.AlgorithmRoot() / "101_Managed_20260715.json",
              R"({"algorithmCode":"101","algorithmName":"Managed","algorithmUsage":1})");
    WriteJson(fix.TemplateRoot() / "202_Template_20260715.json",
              R"({"algorithmCode":"202","algorithmName":"Template","algorithmUsage":1})");
    WriteJson(fix.outside_root / "303_Secret_20260715.json",
              R"({"algorithmCode":"303","algorithmName":"Secret","algorithmUsage":1})");

    SECTION("managed algorithm and template roots remain readable") {
        cosmo::service::algorithm::LayoutDetailResult algorithm_detail;
        REQUIRE(service.GetLayoutDetail("101", fix.AlgorithmRoot().string(), algorithm_detail) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE(algorithm_detail.algorithmName == "Managed");

        cosmo::service::algorithm::LayoutDetailResult template_detail;
        REQUIRE(service.GetLayoutDetail("202", fix.TemplateRoot().string(), template_detail) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE(template_detail.algorithmName == "Template");

        cosmo::service::algorithm::LayoutListResult template_list;
        REQUIRE(service.GetLayoutList("", 1, fix.TemplateRoot().string(), template_list) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE(template_list.list.size() == 1);
        REQUIRE(template_list.list.front().algorithmName == "Template");
    }

    SECTION("portable logical roots resolve under the configured app-data directory") {
        cosmo::service::algorithm::LayoutDetailResult algorithm_detail;
        REQUIRE(service.GetLayoutDetail("101", "", algorithm_detail) == cosmo::util::ErrorEnum::Success);
        REQUIRE(algorithm_detail.algorithmName == "Managed");

        cosmo::service::algorithm::LayoutDetailResult template_detail;
        REQUIRE(service.GetLayoutDetail("202", "algorithm_template", template_detail) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE(template_detail.algorithmName == "Template");

        cosmo::service::algorithm::LayoutListResult template_list;
        REQUIRE(service.GetLayoutList("", 1, "algorithm_template", template_list) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE(template_list.list.size() == 1);
        REQUIRE(template_list.list.front().algorithmName == "Template");
    }

    SECTION("an external directory is rejected") {
        cosmo::service::algorithm::LayoutDetailResult detail;
        REQUIRE(service.GetLayoutDetail("303", fix.outside_root.string(), detail) ==
                cosmo::util::ErrorEnum::InvalidParam);
        REQUIRE(detail.algorithmName.empty());
    }

    SECTION("a nested directory under the algorithm root is rejected") {
        const auto nested = fix.AlgorithmRoot() / "nested";
        WriteJson(nested / "404_Nested_20260715.json",
                  R"({"algorithmCode":"404","algorithmName":"Nested","algorithmUsage":1})");
        cosmo::service::algorithm::LayoutListResult list;
        REQUIRE(service.GetLayoutList("", 1, nested.string(), list) == cosmo::util::ErrorEnum::InvalidParam);
        REQUIRE(list.list.empty());
    }

    SECTION("a symlink alias to a managed directory is rejected") {
        const auto alias = fs::path(cosmo::path::GetResourcePath()) / "template-alias";
        std::error_code error;
        fs::create_directory_symlink(fix.TemplateRoot(), alias, error);
        REQUIRE_FALSE(error);
        cosmo::service::algorithm::LayoutListResult list;
        REQUIRE(service.GetLayoutList("", 1, alias.string(), list) == cosmo::util::ErrorEnum::InvalidParam);
    }

    SECTION("a symlinked JSON member is rejected") {
        const auto linked_member = fix.AlgorithmRoot() / "303_Linked_20260715.json";
        std::error_code error;
        fs::create_symlink(fix.outside_root / "303_Secret_20260715.json", linked_member, error);
        REQUIRE_FALSE(error);
        cosmo::service::algorithm::LayoutDetailResult detail;
        REQUIRE(service.GetLayoutDetail("303", fix.AlgorithmRoot().string(), detail) ==
                cosmo::util::ErrorEnum::InvalidParam);
    }
}

TEST_CASE("Algorithm layout save writes only to the managed algorithm root",
          "[AlgorithmService][security][path]") {
    ManagedAlgorithmPathFixture fix;
    cosmo::service::AlgorithmServiceImpl service;
    cosmo::service::algorithm::LayoutSaveReq request;
    request.algorithmId          = "505";
    request.algorithmCategory    = "1";
    request.algorithmUsage       = "1";
    request.confVersionId        = "default-505";
    request.configVersionName    = "默认";
    request.algorithmMetadata    = "{}";
    request.algorithmProcessdata = "[]";
    request.atomicList           = "[]";

    SECTION("the exact algorithm root is accepted") {
        request.filePath = fix.AlgorithmRoot().string();
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("a hot-reload failure is returned to the caller") {
        request.filePath = fix.AlgorithmRoot().string();
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::FileAnalysisFailed);
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::FileAnalysisFailed);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 0);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("a hot-reload failure restores the previous layout") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{{"algorithmCode", 505},
                                      {"algorithmName", "Existing"},
                                      {"algorithmMetadata", "{\"old\":true}"},
                                      {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::FileAnalysisFailed);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::FileAnalysisFailed);
        nlohmann::json restored;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), restored) ==
                cosmo::util::ErrorEnum::Success);
        CHECK(restored == previous);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("invalid nested metadata is rejected before a new layout is written") {
        request.filePath = fix.AlgorithmRoot().string();
        FORBID_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_));

        request.algorithmMetadata = "{not-json";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail);
        request.algorithmMetadata = R"({"params":{}})";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","legacyChannelEditable":2}]})";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 0);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("invalid nested metadata leaves an existing layout unchanged") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{{"algorithmCode", 505},
                                      {"algorithmName", "Existing"},
                                      {"algorithmMetadata", "{}"},
                                      {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        FORBID_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_));

        request.algorithmMetadata = R"({"params":null})";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail);
        nlohmann::json unchanged;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), unchanged) ==
                cosmo::util::ErrorEnum::Success);
        CHECK(unchanged == previous);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("duplicate metadata keys leave an existing layout unchanged") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{{"algorithmCode", 505},
                                      {"algorithmName", "Existing"},
                                      {"algorithmMetadata", "{}"},
                                      {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        FORBID_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_));

        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"10"},{"key":"param.threshold","value":"20"}]})";
        CHECK(service.LayoutSave(request) == cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail);

        request.algorithmMetadata =
            R"({"params":[{"key":"param.mode","value":"0"},{"key":"param.threshold","value":"10","dependsOn":{"key":"param.mode","value":"1"}},{"key":"param.threshold","value":"20","dependsOn":{"key":"param.mode","value":"2"}}]})";
        CHECK(service.LayoutSave(request) == cosmo::util::ErrorEnum::ActionAlgArrangeConfigFail);

        nlohmann::json unchanged;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), unchanged) ==
                cosmo::util::ErrorEnum::Success);
        CHECK(unchanged == previous);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("a unique conditional metadata parameter can be saved") {
        request.filePath = fix.AlgorithmRoot().string();
        request.algorithmMetadata =
            R"({"params":[{"key":"param.mode","value":"1"},{"key":"param.threshold","value":"10","dependsOn":{"key":"param.mode","value":"1"}}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("a scene save freezes hidden legacy ownership before promotion") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Existing"},
            {"algorithmMetadata",
             R"({"params":[{"key":"param.threshold","value":"10","type":"text","senior":2}]})"},
            {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"20","type":"text","senior":0,"channelEditable":true}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        nlohmann::json saved;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), saved) ==
                cosmo::util::ErrorEnum::Success);
        const auto metadata = nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>());
        REQUIRE(metadata.at("params").size() == 1);
        CHECK(metadata.at("params").at(0).at("legacyChannelEditable") == false);
    }

    SECTION("a scene save freezes visible legacy ownership before explicit metadata") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Existing"},
            {"algorithmMetadata",
             R"({"params":[{"key":"param.threshold","value":"10","type":"text","senior":0}]})"},
            {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"20","type":"text","senior":0,"channelEditable":true}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        nlohmann::json saved;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), saved) ==
                cosmo::util::ErrorEnum::Success);
        const auto metadata = nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>());
        REQUIRE(metadata.at("params").size() == 1);
        CHECK(metadata.at("params").at(0).at("legacyChannelEditable") == true);
    }

    SECTION("a forged hint cannot grant a new ordinary parameter legacy eligibility") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{{"algorithmCode", 505},
                                      {"algorithmName", "Existing"},
                                      {"algorithmMetadata", R"({"params":[]})"},
                                      {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.newThreshold","value":"20","type":"text","senior":0,"channelEditable":true,"legacyChannelEditable":true}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        nlohmann::json saved;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), saved) ==
                cosmo::util::ErrorEnum::Success);
        const auto metadata = nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>());
        CHECK(metadata.at("params").at(0).at("legacyChannelEditable") == false);
    }

    SECTION("legacy eligibility stays revoked after demotion and later promotion") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Existing"},
            {"algorithmMetadata",
             R"({"params":[{"key":"param.threshold","value":"10","type":"text","senior":0}]})"},
            {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success)
            .TIMES(2);

        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"20","type":"text","senior":2,"channelEditable":false}]})";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);

        // Even a client-supplied hint cannot revive a stale markerless task value.
        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"30","type":"text","senior":0,"channelEditable":true,"legacyChannelEditable":true}]})";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);

        nlohmann::json saved;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), saved) ==
                cosmo::util::ErrorEnum::Success);
        const auto metadata = nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>());
        CHECK(metadata.at("params").at(0).at("legacyChannelEditable") == false);
    }

    SECTION("frozen eligibility survives a dependency re-parent without being re-inferred") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Existing"},
            {"algorithmMetadata",
             R"({"params":[{"key":"param.oldParent","value":"1","type":"switch","senior":0},{"key":"param.newParent","value":"0","type":"switch","senior":2},{"key":"param.child","value":"10","type":"text","senior":0,"dependsOn":{"key":"param.oldParent","value":"1"}}]})"},
            {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success)
            .TIMES(2);

        request.algorithmMetadata =
            R"({"params":[{"key":"param.oldParent","value":"1","type":"switch","senior":2,"channelEditable":false,"legacyChannelEditable":false},{"key":"param.newParent","value":"1","type":"switch","senior":0,"channelEditable":true,"legacyChannelEditable":true},{"key":"param.child","value":"20","type":"text","senior":0,"channelEditable":true,"legacyChannelEditable":false,"dependsOn":{"key":"param.newParent","value":"1"}}]})";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);

        nlohmann::json saved;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), saved) ==
                cosmo::util::ErrorEnum::Success);
        const auto params =
            nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>()).at("params");
        REQUIRE(params.size() == 3);
        CHECK(params.at(0).at("legacyChannelEditable") == false);
        CHECK(params.at(1).at("legacyChannelEditable") == false);
        CHECK(params.at(2).at("legacyChannelEditable") == true);
    }

    SECTION("old-format layouts remain the ownership source during filename migration") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto legacy_path = fix.AlgorithmRoot() / "505.json";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Legacy"},
            {"algorithmMetadata",
             R"({"params":[{"key":"param.threshold","value":"10","type":"text","senior":0}]})"},
            {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(legacy_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"20","type":"text","senior":0,"channelEditable":true}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 2);
        nlohmann::json saved;
        for (const auto& entry : fs::directory_iterator(fix.AlgorithmRoot())) {
            if (entry.path() == legacy_path) {
                continue;
            }
            REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(entry.path().string(), saved) ==
                    cosmo::util::ErrorEnum::Success);
        }
        const auto metadata = nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>());
        CHECK(metadata.at("params").at(0).at("legacyChannelEditable") == true);
    }

    SECTION("old-format migration rollback preserves the source and removes the new target") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto legacy_path = fix.AlgorithmRoot() / "505.json";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Legacy"},
            {"algorithmMetadata",
             R"({"params":[{"key":"param.threshold","value":"10","type":"text","senior":0}]})"},
            {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(legacy_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"20","type":"text","senior":0,"channelEditable":true}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::FileAnalysisFailed);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::FileAnalysisFailed);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
        nlohmann::json restored;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(legacy_path.string(), restored) ==
                cosmo::util::ErrorEnum::Success);
        CHECK(restored == previous);
    }

    SECTION("deterministic legacy compatibility controls retain migration eligibility") {
        request.filePath       = fix.AlgorithmRoot().string();
        const auto layout_path = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Existing"},
            {"algorithmMetadata",
             R"({"params":[{"key":"param.retro","value":"1","type":"retroDirect","senior":1,"channelEditable":true}]})"},
            {"configVersionList", nlohmann::json::array()}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.retro","value":"2","type":"retroDirect","senior":1,"channelEditable":true},{"key":"param.videoRepeatCount","value":"3","type":"text","senior":2,"channelEditable":true}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        nlohmann::json saved;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), saved) ==
                cosmo::util::ErrorEnum::Success);
        const auto params =
            nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>()).at("params");
        REQUIRE(params.size() == 2);
        CHECK(params.at(0).at("legacyChannelEditable") == true);
        CHECK(params.at(1).at("legacyChannelEditable") == true);
    }

    SECTION("a historical configuration version cannot revive globally revoked eligibility") {
        request.filePath          = fix.AlgorithmRoot().string();
        request.confVersionId     = "alternate-505";
        request.configVersionName = "Alternate";
        const auto layout_path    = fix.AlgorithmRoot() / "505_Existing_20260715.json";
        const auto hiddenMetadata =
            R"({"params":[{"key":"param.threshold","value":"10","type":"text","senior":2,"legacyChannelEditable":false}]})";
        const auto visibleMetadata =
            R"({"params":[{"key":"param.threshold","value":"11","type":"text","senior":0,"legacyChannelEditable":true}]})";
        const nlohmann::json previous{
            {"algorithmCode", 505},
            {"algorithmName", "Existing"},
            {"algorithmMetadata", hiddenMetadata},
            {"configVersionList",
             nlohmann::json::array({nlohmann::json{{"id", "alternate-505"},
                                                    {"name", "Alternate"},
                                                    {"algorithmMetadata", visibleMetadata}}})}};
        REQUIRE(cosmo::util::JsonFileUtil::WriteJsonFile(layout_path.string(), previous) ==
                cosmo::util::ErrorEnum::Success);
        request.algorithmMetadata =
            R"({"params":[{"key":"param.threshold","value":"20","type":"text","senior":0,"channelEditable":true}]})";
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);

        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        nlohmann::json saved;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(layout_path.string(), saved) ==
                cosmo::util::ErrorEnum::Success);
        const auto topMetadata =
            nlohmann::json::parse(saved.at("algorithmMetadata").get<std::string>());
        CHECK(topMetadata.at("params").at(0).at("legacyChannelEditable") == false);
        REQUIRE(saved.at("configVersionList").size() == 1);
        const auto versionMetadata = nlohmann::json::parse(
            saved.at("configVersionList").at(0).at("algorithmMetadata").get<std::string>());
        CHECK(versionMetadata.at("params").at(0).at("legacyChannelEditable") == false);
    }

    SECTION("the portable default algorithm root is accepted") {
        request.filePath.clear();
        REQUIRE_CALL(fix.mocks.algSvc, ReloadAlgorithmFromFile(trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success);
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::Success);
        REQUIRE(CountRegularFiles(fix.AlgorithmRoot()) == 1);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("an external directory is rejected") {
        request.filePath = fix.outside_root.string();
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::InvalidParam);
        REQUIRE(CountRegularFiles(fix.outside_root) == 0);
    }

    SECTION("the template root is read-only for layout operations") {
        request.filePath = fix.TemplateRoot().string();
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::InvalidParam);
        REQUIRE(CountRegularFiles(fix.TemplateRoot()) == 0);
    }

    SECTION("an unsafe algorithm identifier is rejected") {
        request.filePath    = fix.AlgorithmRoot().string();
        request.algorithmId = "../../escaped";
        REQUIRE(service.LayoutSave(request) == cosmo::util::ErrorEnum::InvalidParam);
        REQUIRE_FALSE(fs::exists(fix.root / "escaped.json"));
    }
}

TEST_CASE("Algorithm atomic actions accept only the managed actions file",
          "[AlgorithmService][security][path]") {
    ManagedAlgorithmPathFixture fix;
    cosmo::service::AlgorithmServiceImpl service;
    const auto actions_path = fix.LayoutRoot() / "actions.json";
    WriteJson(actions_path, R"([{"id":"action-1","name":"Managed action","actionUsage":1,"actionType":2}])");
    const auto external_actions = fix.outside_root / "actions.json";
    WriteJson(external_actions,
              R"([{"id":"secret","name":"External action","actionUsage":1,"actionType":2}])");

    SECTION("the default and exact managed paths are accepted") {
        cosmo::service::algorithm::AtomicActionListResult default_result;
        REQUIRE(service.GetAtomicActionList(1, "", default_result) == cosmo::util::ErrorEnum::Success);
        REQUIRE(default_result.list.size() == 1);
        REQUIRE(default_result.list.front().id == "action-1");

        cosmo::service::algorithm::AtomicActionListResult exact_result;
        REQUIRE(service.GetAtomicActionList(1, actions_path.string(), exact_result) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE(exact_result.list.size() == 1);
    }

    SECTION("an external action file is rejected") {
        cosmo::service::algorithm::AtomicActionListResult result;
        REQUIRE(service.GetAtomicActionList(1, external_actions.string(), result) ==
                cosmo::util::ErrorEnum::InvalidParam);
        REQUIRE(result.list.empty());
    }

    SECTION("a nested action file is rejected") {
        const auto nested_actions = fix.LayoutRoot() / "nested" / "actions.json";
        WriteJson(nested_actions, R"([])");
        cosmo::service::algorithm::AtomicActionListResult result;
        REQUIRE(service.GetAtomicActionList(1, nested_actions.string(), result) ==
                cosmo::util::ErrorEnum::InvalidParam);
    }

    SECTION("a symlink alias to the managed action file is rejected") {
        const auto alias = fix.LayoutRoot() / "actions-alias.json";
        std::error_code error;
        fs::create_symlink(actions_path, alias, error);
        REQUIRE_FALSE(error);
        cosmo::service::algorithm::AtomicActionListResult result;
        REQUIRE(service.GetAtomicActionList(1, alias.string(), result) ==
                cosmo::util::ErrorEnum::InvalidParam);
    }
}

TEST_CASE("Algorithm services reject a symlinked managed algorithm root",
          "[AlgorithmService][security][path]") {
    ManagedAlgorithmPathFixture fix;
    const auto replacement = fix.outside_root / "replacement";
    fs::create_directories(replacement);
    std::error_code error;
    fs::remove_all(fix.AlgorithmRoot(), error);
    REQUIRE_FALSE(error);
    fs::create_directory_symlink(replacement, fix.AlgorithmRoot(), error);
    REQUIRE_FALSE(error);

    cosmo::service::AlgorithmServiceImpl service;
    REQUIRE(service.AddFromJson("ManagedAlgorithm", 1, 1, "", "", "") ==
            cosmo::util::ErrorEnum::InvalidParam);
    cosmo::service::algorithm::LayoutListResult list;
    REQUIRE(service.GetLayoutList("", 1, "", list) == cosmo::util::ErrorEnum::InvalidParam);
    REQUIRE(CountRegularFiles(replacement) == 0);
}
