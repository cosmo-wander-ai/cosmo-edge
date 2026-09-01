#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "catch_amalgamated.hpp"
#include "mock/MockAlgorithmService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockTaskService.h"
#include "service/camera/impl/CameraTaskUnit.h"
#include "util/JsonStructUtil.h"

using namespace cosmo;
using trompeloeil::_;

namespace {
MsgDynamicKeyValue MakeParam(const std::string& key, const std::string& value) {
    MsgDynamicKeyValue param;
    param.key   = key;
    param.value = value;
    return param;
}

std::string FindParamValue(const std::vector<MsgDynamicKeyValue>& params, const std::string& key) {
    auto it = std::find_if(params.begin(), params.end(), [&](const auto& param) { return param.key == key; });
    return it == params.end() ? std::string{} : it->value.ToString();
}

std::string FindParamValue(const MsgTaskConfig& config, const std::string& key) {
    auto it = std::find_if(config.params.begin(), config.params.end(),
                           [&](const auto& param) { return param.key == key; });
    return it == config.params.end() ? std::string{} : it->value.ToString();
}

bool HasParam(const std::vector<MsgDynamicKeyValue>& params, const std::string& key) {
    return std::any_of(params.begin(), params.end(), [&](const auto& param) { return param.key == key; });
}

bool HasOverrideKey(const CameraTaskUnitParam& params, const std::string& key) {
    return std::find(params.channelOverrideKeys.begin(), params.channelOverrideKeys.end(), key) !=
           params.channelOverrideKeys.end();
}

MsgDynamicElement MakeMetadataParam(const std::string& key, const std::string& value,
                                    std::optional<bool> channel_editable,
                                    std::optional<int> senior = std::nullopt,
                                    const std::string& type   = "text") {
    MsgDynamicElement param;
    param.key             = key;
    param.value           = value;
    param.defaultValue    = value;
    param.type            = type;
    param.channelEditable = channel_editable;
    param.senior          = senior;
    return param;
}

std::string MakeMetadataJson(std::vector<MsgDynamicElement> params) {
    MsgAlgorithmMetaData metadata;
    metadata.params = std::move(params);
    std::string encoded;
    if (!util::EncodeJson(metadata, encoded)) {
        return {};
    }
    return encoded;
}

bool SeedSavedParams(const std::filesystem::path& config_root, const std::string& algorithm_code,
                     std::vector<MsgDynamicKeyValue> params,
                     std::optional<std::vector<std::string>> channel_override_keys = std::nullopt) {
    const auto task_config_dir = config_root / algorithm_code;
    std::error_code error;
    std::filesystem::create_directories(task_config_dir, error);
    if (error) {
        return false;
    }
    nlohmann::json persisted{{"params", std::move(params)}};
    if (channel_override_keys.has_value()) {
        persisted["channelOverrideKeys"] = std::move(*channel_override_keys);
    }
    return util::SaveStructToJsonFile((task_config_dir / "param.json").string(), persisted);
}

CameraTaskUnitParam LoadSavedParams(const std::filesystem::path& config_root,
                                    const std::string& algorithm_code) {
    CameraTaskUnitParam persisted;
    REQUIRE(util::LoadStructFromJsonFile((config_root / algorithm_code / "param.json").string(), persisted));
    return persisted;
}
}  // namespace

TEST_CASE("CameraTaskUnit initializes a new task from the scene metadata value",
          "[CameraTaskUnit][task-parameters][ownership]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_unit_scene_baseline";
    std::filesystem::remove_all(config_root);

    auto scene_param         = MakeMetadataParam("param.sceneOwned", "11", false, 2);
    scene_param.defaultValue = "10";
    const auto metadata      = MakeMetadataJson({scene_param});

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(metadata);

    std::vector<MsgTaskConfig> applied;
    ALLOW_CALL(mocks.taskSvc, SetTaskParam("scene_channel", "scene_channel_test_alg", _))
        .LR_SIDE_EFFECT(applied.push_back(_3))
        .RETURN(true);

    CameraTaskUnit unit(config_root.string(), "scene_channel", "test_alg", {});

    REQUIRE(unit.IsReady());
    CHECK(FindParamValue(unit.GetParams(), "param.sceneOwned") == "11");
    REQUIRE(applied.size() == 1);
    CHECK(FindParamValue(applied.back(), "param.sceneOwned") == "11");
}

TEST_CASE("CameraTaskUnit applies only channel-editable values over the scene baseline",
          "[CameraTaskUnit][task-parameters][ownership][migration]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_unit_ownership";
    std::filesystem::remove_all(config_root);
    REQUIRE(SeedSavedParams(config_root, "test_alg",
                            {MakeParam("param.sceneOwned", "5"), MakeParam("param.visible", "6")}));

    const auto metadata = MakeMetadataJson({MakeMetadataParam("param.sceneOwned", "11", false, 2),
                                            MakeMetadataParam("param.visible", "10", std::nullopt, 0)});
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(metadata);

    std::vector<MsgTaskConfig> applied;
    ALLOW_CALL(mocks.taskSvc, SetTaskParam("ownership_channel", "ownership_channel_test_alg", _))
        .LR_SIDE_EFFECT(applied.push_back(_3))
        .RETURN(true);

    CameraTaskUnit unit(config_root.string(), "ownership_channel", "test_alg", {});
    REQUIRE(unit.IsReady());
    CHECK(FindParamValue(unit.GetParams(), "param.sceneOwned") == "11");
    CHECK(FindParamValue(unit.GetParams(), "param.visible") == "6");
    REQUIRE(applied.size() == 1);
    CHECK(FindParamValue(applied.back(), "param.sceneOwned") == "11");
    CHECK(FindParamValue(applied.back(), "param.visible") == "6");

    MsgTaskConfig channel_request;
    channel_request.params = {MakeParam("param.sceneOwned", "7"), MakeParam("param.visible", "8")};
    REQUIRE(unit.SetChannelParams(channel_request) == util::ErrorEnum::Success);

    CHECK(FindParamValue(unit.GetParams(), "param.sceneOwned") == "11");
    CHECK(FindParamValue(unit.GetParams(), "param.visible") == "8");
    CHECK(unit.ApplyLatestTaskConfig());
    REQUIRE(applied.size() == 2);
    CHECK(FindParamValue(applied.back(), "param.sceneOwned") == "11");
    CHECK(FindParamValue(applied.back(), "param.visible") == "8");
}

TEST_CASE("CameraTaskUnit refreshes scene-owned values while retaining visible overrides after rebuild",
          "[CameraTaskUnit][task-parameters][ownership][reload]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_unit_scene_reload";
    std::filesystem::remove_all(config_root);
    REQUIRE(SeedSavedParams(config_root, "test_alg",
                            {MakeParam("param.sceneOwned", "5"), MakeParam("param.visible", "6")},
                            std::vector<std::string>{"param.visible"}));

    {
        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.sceneOwned", "10", false, 2),
                                      MakeMetadataParam("param.visible", "1", true, 0)}));
        CameraTaskUnit unit(config_root.string(), "reload_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.sceneOwned") == "10");
        CHECK(FindParamValue(unit.GetParams(), "param.visible") == "6");

        MsgTaskConfig request;
        request.params = {MakeParam("param.sceneOwned", "7"), MakeParam("param.visible", "8")};
        REQUIRE(unit.SetChannelParams(request) == util::ErrorEnum::Success);
        CHECK(FindParamValue(unit.GetParams(), "param.sceneOwned") == "10");
        CHECK(FindParamValue(unit.GetParams(), "param.visible") == "8");
    }

    {
        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.sceneOwned", "12", false, 2),
                                      MakeMetadataParam("param.visible", "1", true, 0)}));

        MsgTaskConfig rebuilt_apply;
        ALLOW_CALL(mocks.taskSvc, SetTaskParam("reload_channel", "reload_channel_test_alg", _))
            .LR_SIDE_EFFECT(rebuilt_apply = _3)
            .RETURN(true);

        CameraTaskUnit unit(config_root.string(), "reload_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.sceneOwned") == "12");
        CHECK(FindParamValue(unit.GetParams(), "param.visible") == "8");
        CHECK(FindParamValue(rebuilt_apply, "param.sceneOwned") == "12");
        CHECK(FindParamValue(rebuilt_apply, "param.visible") == "8");
    }
}

TEST_CASE("CameraTaskUnit migrates legacy snapshots using previous channel visibility",
          "[CameraTaskUnit][task-parameters][ownership][migration]") {
    SECTION("a frozen legacy-editable descriptor retains its prior channel value") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_explicit_editable";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg", {MakeParam("param.threshold", "10")}));

        auto metadataParam                  = MakeMetadataParam("param.threshold", "20", true, 0);
        metadataParam.legacyChannelEditable = true;
        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeMetadataJson({metadataParam}));

        CameraTaskUnit unit(config_root.string(), "legacy_explicit_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "10");

        const auto canonical = LoadSavedParams(config_root, "test_alg");
        CHECK(canonical.channelOverrideKeysPresent);
        REQUIRE(canonical.channelOverrideKeys.size() == 1);
        CHECK(canonical.channelOverrideKeys.front() == "param.threshold");
    }

    SECTION("the real UI promotion shape starts from the latest scene value") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_promoted_editable";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg", {MakeParam("param.threshold", "10")}));

        auto metadataParam                  = MakeMetadataParam("param.threshold", "20", true, 0);
        metadataParam.legacyChannelEditable = false;
        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeMetadataJson({metadataParam}));

        CameraTaskUnit unit(config_root.string(), "legacy_promoted_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "20");

        const auto canonical = LoadSavedParams(config_root, "test_alg");
        CHECK(canonical.channelOverrideKeysPresent);
        CHECK(canonical.channelOverrideKeys.empty());
    }

    SECTION("explicit current ownership without frozen evidence fails closed") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_ambiguous_explicit";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg", {MakeParam("param.threshold", "10")}));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "20", true, 0)}));

        CameraTaskUnit unit(config_root.string(), "legacy_ambiguous_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "20");
        CHECK(LoadSavedParams(config_root, "test_alg").channelOverrideKeys.empty());
    }

    SECTION("an implicitly editable legacy descriptor retains its prior channel value") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_implicit_editable";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg", {MakeParam("param.threshold", "10")}));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "20", std::nullopt, 0)}));

        CameraTaskUnit unit(config_root.string(), "legacy_implicit_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "10");

        const auto canonical = LoadSavedParams(config_root, "test_alg");
        REQUIRE(canonical.channelOverrideKeys.size() == 1);
        CHECK(canonical.channelOverrideKeys.front() == "param.threshold");
    }

    SECTION("frozen child eligibility is not recomputed through a new parent") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_reparented_child";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg",
                                {MakeParam("param.newParent", "0"), MakeParam("param.child", "10")}));

        auto parent                  = MakeMetadataParam("param.newParent", "1", true, 0, "switch");
        parent.legacyChannelEditable = false;
        auto child                   = MakeMetadataParam("param.child", "20", true, 0);
        child.legacyChannelEditable  = true;
        child.dependsOn.key          = "param.newParent";
        child.dependsOn.value        = "1";
        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeMetadataJson({parent, child}));

        CameraTaskUnit unit(config_root.string(), "legacy_reparented_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.newParent") == "1");
        CHECK(FindParamValue(unit.GetParams(), "param.child") == "10");

        const auto canonical = LoadSavedParams(config_root, "test_alg");
        REQUIRE(canonical.channelOverrideKeys.size() == 1);
        CHECK(canonical.channelOverrideKeys.front() == "param.child");
    }

    SECTION("legacy compatibility descriptors remain eligible without a frozen hint") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_explicit_compatibility";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg",
                                {MakeParam("param.videoRepeatCount", "3"), MakeParam("param.retro", "4")}));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.videoRepeatCount", "30", true, 2),
                                      MakeMetadataParam("param.retro", "40", true, 1, "retroDirect")}));

        CameraTaskUnit unit(config_root.string(), "legacy_compatibility_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.videoRepeatCount") == "3");
        CHECK(FindParamValue(unit.GetParams(), "param.retro") == "4");

        const auto canonical = LoadSavedParams(config_root, "test_alg");
        CHECK(HasOverrideKey(canonical, "param.videoRepeatCount"));
        CHECK(HasOverrideKey(canonical, "param.retro"));
    }

    SECTION("an unreadable provenance marker falls back to the scene baseline") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_invalid_legacy_marker";
        std::filesystem::remove_all(config_root);
        const auto task_config_dir = config_root / "test_alg";
        std::filesystem::create_directories(task_config_dir);
        const nlohmann::json invalid_snapshot{
            {"params", nlohmann::json::array({MakeParam("param.threshold", "10")})},
            {"channelOverrideKeys", "not-an-array"}};
        REQUIRE(util::SaveStructToJsonFile((task_config_dir / "param.json").string(), invalid_snapshot));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "20", true, 0)}));

        CameraTaskUnit unit(config_root.string(), "invalid_legacy_marker_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "20");

        const auto canonical = LoadSavedParams(config_root, "test_alg");
        CHECK(canonical.channelOverrideKeysPresent);
        CHECK(canonical.channelOverrideKeys.empty());
    }

    SECTION("only the key-level compatibility item survives without a descriptor") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_unknown_compatibility";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg",
                                {MakeParam("param.videoRepeatCount", "3"),
                                 MakeParam("param.retroDirect", "4"), MakeParam("param.unknown", "5")}));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeMetadataJson({}));

        CameraTaskUnit unit(config_root.string(), "legacy_unknown_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        const auto params = unit.GetParams();
        REQUIRE(params.size() == 1);
        CHECK(FindParamValue(params, "param.videoRepeatCount") == "3");
        const auto canonical = LoadSavedParams(config_root, "test_alg");
        REQUIRE(canonical.channelOverrideKeys.size() == 1);
        CHECK(canonical.channelOverrideKeys.front() == "param.videoRepeatCount");
    }
}

TEST_CASE("CameraTaskUnit uses only canonical override markers after migration",
          "[CameraTaskUnit][task-parameters][ownership][provenance]") {
    SECTION("a marked override wins over the latest scene baseline") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_marked_override";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg", {MakeParam("param.threshold", "10")},
                                std::vector<std::string>{"param.threshold"}));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "20", true)}));
        CameraTaskUnit unit(config_root.string(), "marked_override_channel", "test_alg", {});

        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "10");
    }

    SECTION("an explicit empty marker follows scene updates") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_empty_override_marker";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg", {MakeParam("param.threshold", "10")},
                                std::vector<std::string>{}));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
            .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "20", true)}));
        CameraTaskUnit unit(config_root.string(), "empty_marker_channel", "test_alg", {});

        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "20");
    }

    SECTION("saving the baseline value still records an explicit channel override") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_baseline_override_marker";
        std::filesystem::remove_all(config_root);

        {
            cosmo::test::MockServiceRegistry mocks;
            ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
                .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "20", true)}));
            CameraTaskUnit unit(config_root.string(), "baseline_override_channel", "test_alg", {});
            REQUIRE(unit.SetChannelParams(std::vector<MsgDynamicKeyValue>{
                        MakeParam("param.threshold", "20")}) == util::ErrorEnum::Success);
            CHECK(HasOverrideKey(LoadSavedParams(config_root, "test_alg"), "param.threshold"));
        }

        {
            cosmo::test::MockServiceRegistry mocks;
            ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
                .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "30", true)}));
            CameraTaskUnit unit(config_root.string(), "baseline_override_channel", "test_alg", {});
            CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "20");
        }
    }

    SECTION("an editable key becoming hidden loses its old override") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_override_becomes_hidden";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(config_root, "test_alg", {MakeParam("param.threshold", "10")},
                                std::vector<std::string>{"param.threshold"}));

        {
            cosmo::test::MockServiceRegistry mocks;
            ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
                .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "20", false)}));
            CameraTaskUnit unit(config_root.string(), "hidden_override_channel", "test_alg", {});

            REQUIRE(unit.IsReady());
            CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "20");
            CHECK(LoadSavedParams(config_root, "test_alg").channelOverrideKeys.empty());
        }

        {
            cosmo::test::MockServiceRegistry mocks;
            ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
                .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "30", true)}));
            CameraTaskUnit unit(config_root.string(), "hidden_override_channel", "test_alg", {});

            REQUIRE(unit.IsReady());
            CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "30");
        }
    }

    SECTION("deleted and unknown keys are removed except the key-level compatibility item") {
        const std::filesystem::path config_root =
            "/tmp/cosmo_test/conf/camera/test_task_unit_deleted_override";
        std::filesystem::remove_all(config_root);
        REQUIRE(SeedSavedParams(
            config_root, "test_alg",
            {MakeParam("param.deleted", "1"), MakeParam("param.unknown", "2"),
             MakeParam("param.videoRepeatCount", "3"), MakeParam("param.videoRepeatCount", "9"),
             MakeParam("param.retroDirect", "4")},
            std::vector<std::string>{"param.deleted", "param.unknown", "param.videoRepeatCount",
                                     "param.videoRepeatCount", "param.retroDirect"}));

        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(MakeMetadataJson({}));
        CameraTaskUnit unit(config_root.string(), "deleted_override_channel", "test_alg", {});

        REQUIRE(unit.IsReady());
        const auto params = unit.GetParams();
        REQUIRE(params.size() == 1);
        CHECK(FindParamValue(params, "param.videoRepeatCount") == "3");
        const auto canonical = LoadSavedParams(config_root, "test_alg");
        REQUIRE(canonical.channelOverrideKeys.size() == 1);
        CHECK(canonical.channelOverrideKeys.front() == "param.videoRepeatCount");
    }
}

TEST_CASE("CameraTaskUnit persists provenance created by channel and trusted binding updates",
          "[CameraTaskUnit][task-parameters][ownership][provenance][reload]") {
    const std::filesystem::path config_root =
        "/tmp/cosmo_test/conf/camera/test_task_unit_new_override_provenance";
    std::filesystem::remove_all(config_root);
    const auto metadata = MakeMetadataJson({MakeMetadataParam("param.threshold", "5", true),
                                            MakeMetadataParam("param.faceSet", "", true, 0, "faceSet"),
                                            MakeMetadataParam("param.sceneOwned", "20", false)});

    {
        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(metadata);
        CameraTaskUnit unit(config_root.string(), "new_override_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        REQUIRE(unit.SetChannelParams(std::vector<MsgDynamicKeyValue>{MakeParam("param.threshold", "7")}) ==
                util::ErrorEnum::Success);
        MsgTaskConfig trustedBinding;
        trustedBinding.params = {MakeParam("param.faceSet", "lib-a"), MakeParam("param.sceneOwned", "9"),
                                 MakeParam("param.unknown", "8")};
        REQUIRE(unit.SetParams(trustedBinding) == util::ErrorEnum::Success);

        const auto persisted = LoadSavedParams(config_root, "test_alg");
        CHECK(HasOverrideKey(persisted, "param.threshold"));
        CHECK(HasOverrideKey(persisted, "param.faceSet"));
        CHECK_FALSE(HasOverrideKey(persisted, "param.sceneOwned"));
        CHECK_FALSE(HasOverrideKey(persisted, "param.unknown"));
        CHECK(FindParamValue(persisted.params, "param.sceneOwned") == "20");
        CHECK_FALSE(HasParam(persisted.params, "param.unknown"));
    }

    {
        cosmo::test::MockServiceRegistry mocks;
        ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(metadata);
        CameraTaskUnit unit(config_root.string(), "new_override_channel", "test_alg", {});
        REQUIRE(unit.IsReady());
        CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "7");
        CHECK(FindParamValue(unit.GetParams(), "param.faceSet") == "lib-a");
    }
}

TEST_CASE("CameraTaskUnit fails closed when parameter ownership metadata is invalid",
          "[CameraTaskUnit][task-parameters][ownership][validation]") {
    struct InvalidMetadataCase {
        std::string name;
        std::string metadata;
    };
    const std::vector<InvalidMetadataCase> cases{
        {"malformed JSON", "{not-json"},
        {"null root", "null"},
        {"array root", "[]"},
        {"non-array params", R"({"params":{}})"},
        {"duplicate parameter keys", MakeMetadataJson({MakeMetadataParam("param.duplicate", "1", true),
                                                       MakeMetadataParam("param.duplicate", "2", true)})},
    };

    for (size_t index = 0; index < cases.size(); ++index) {
        const auto& test_case = cases[index];
        DYNAMIC_SECTION(test_case.name) {
            const auto config_root = std::filesystem::path("/tmp/cosmo_test/conf/camera") /
                                     ("test_task_unit_bad_metadata_" + std::to_string(index));
            std::filesystem::remove_all(config_root);

            cosmo::test::MockServiceRegistry mocks;
            ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(test_case.metadata);
            FORBID_CALL(mocks.taskSvc, TaskCreate(_, _, _, _));
            FORBID_CALL(mocks.taskSvc, SetTaskParam(_, _, _));

            CameraTaskUnit unit(config_root.string(), "bad_metadata_channel_" + std::to_string(index),
                                "test_alg", {});

            CHECK_FALSE(unit.IsReady());
            CHECK(unit.GetStatus() == util::ErrorEnum::ActionAlgArrangeConfigFail);
            CHECK(unit.GetParams().empty());
        }
    }
}

TEST_CASE("CameraTaskUnit accepts empty object metadata roots",
          "[CameraTaskUnit][task-parameters][ownership][validation]") {
    const std::vector<std::string> cases{"{}", R"({"params":[]})"};

    for (size_t index = 0; index < cases.size(); ++index) {
        DYNAMIC_SECTION("valid empty metadata " << index) {
            const auto config_root = std::filesystem::path("/tmp/cosmo_test/conf/camera") /
                                     ("test_task_unit_empty_metadata_" + std::to_string(index));
            std::filesystem::remove_all(config_root);

            cosmo::test::MockServiceRegistry mocks;
            ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg")).RETURN(cases[index]);

            CameraTaskUnit unit(config_root.string(), "empty_metadata_channel_" + std::to_string(index),
                                "test_alg", {});

            CHECK(unit.IsReady());
            CHECK(unit.GetStatus() == util::ErrorEnum::Success);
            CHECK(unit.GetParams().empty());
        }
    }
}

TEST_CASE("CameraTaskUnit rejects ambiguous duplicate channel parameter keys before mutation",
          "[CameraTaskUnit][task-parameters][ownership][validation]") {
    const std::filesystem::path config_root =
        "/tmp/cosmo_test/conf/camera/test_task_unit_duplicate_channel_keys";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
        .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "5", true)}));
    CameraTaskUnit unit(config_root.string(), "duplicate_key_channel", "test_alg", {});
    REQUIRE(unit.IsReady());

    MsgTaskArea rejectedArea;
    rejectedArea.areaId = "must-not-be-saved";
    MsgTaskConfig duplicateSnapshot;
    duplicateSnapshot.params = {MakeParam("param.threshold", "6"), MakeParam("param.threshold", "7")};
    duplicateSnapshot.areas  = {rejectedArea};
    REQUIRE(unit.SetChannelParams(duplicateSnapshot) == util::ErrorEnum::InvalidParam);
    CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "5");

    std::vector<MsgTaskArea> areas;
    std::vector<MsgTaskArea> shieldedAreas;
    REQUIRE(unit.GetArea(areas, shieldedAreas) == util::ErrorEnum::Success);
    CHECK(areas.empty());
    CHECK(shieldedAreas.empty());

    REQUIRE(unit.SetChannelParams(std::vector<MsgDynamicKeyValue>{MakeParam("param.threshold", "8"),
                                                                  MakeParam("param.threshold", "9")}) ==
            util::ErrorEnum::InvalidParam);
    CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "5");
}

TEST_CASE("CameraTaskUnit keeps legacy and special channel parameters editable",
          "[CameraTaskUnit][task-parameters][ownership][compatibility]") {
    const std::filesystem::path config_root = "/tmp/cosmo_test/conf/camera/test_task_unit_legacy_ownership";
    std::filesystem::remove_all(config_root);
    REQUIRE(SeedSavedParams(config_root, "test_alg",
                            {MakeParam("param.legacy", "1"), MakeParam("param.retroDirect", "2"),
                             MakeParam("param.explicitRetro", "4"), MakeParam("param.noDescriptor", "5")}));

    auto legacy         = MakeMetadataParam("param.legacy", "10", std::nullopt);
    auto retro          = MakeMetadataParam("param.retroDirect", "20", std::nullopt, 1, "retroDirect");
    auto explicit_retro = MakeMetadataParam("param.explicitRetro", "40", false, 0, "retroDirect");

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
        .RETURN(MakeMetadataJson({legacy, retro, explicit_retro}));

    CameraTaskUnit unit(config_root.string(), "legacy_channel", "test_alg", {});
    REQUIRE(unit.IsReady());
    const auto params = unit.GetParams();

    CHECK(FindParamValue(params, "param.legacy") == "1");
    CHECK(FindParamValue(params, "param.retroDirect") == "2");
    CHECK(FindParamValue(params, "param.explicitRetro") == "40");
    CHECK_FALSE(HasParam(params, "param.noDescriptor"));

    REQUIRE(unit.SetChannelParams(std::vector<MsgDynamicKeyValue>{
                MakeParam("param.injected", "99"), MakeParam("param.noDescriptor", "6"),
                MakeParam("param.videoRepeatCount", "3")}) == util::ErrorEnum::Success);
    const auto updated = unit.GetParams();
    CHECK_FALSE(HasParam(updated, "param.injected"));
    CHECK_FALSE(HasParam(updated, "param.noDescriptor"));
    CHECK(FindParamValue(updated, "param.videoRepeatCount") == "3");
}

TEST_CASE("CameraTaskUnit restores exact channel snapshots when persistence fails",
          "[CameraTaskUnit][task-parameters][persistence]") {
    const std::filesystem::path config_root =
        "/tmp/cosmo_test/conf/camera/test_task_unit_transactional_persistence";
    std::filesystem::remove_all(config_root);

    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.algSvc, GetMetaData("test_alg"))
        .RETURN(MakeMetadataJson({MakeMetadataParam("param.threshold", "5", true),
                                  MakeMetadataParam("param.secondary", "1", true)}));
    size_t appliedCount = 0;
    ALLOW_CALL(mocks.taskSvc, SetTaskParam("persistence_channel", "persistence_channel_test_alg", _))
        .LR_SIDE_EFFECT(appliedCount += 1)
        .RETURN(true);
    CameraTaskUnit unit(config_root.string(), "persistence_channel", "test_alg", {});

    MsgTaskArea oldArea;
    oldArea.areaId = "old-area";
    MsgTaskArea oldShieldedArea;
    oldShieldedArea.areaId = "old-shielded-area";
    MsgTaskConfig initial;
    initial.params        = {MakeParam("param.threshold", "6")};
    initial.areas         = {oldArea};
    initial.shieldedAreas = {oldShieldedArea};
    REQUIRE(unit.SetChannelParams(initial) == util::ErrorEnum::Success);
    CHECK(unit.ApplyLatestTaskConfig());
    const auto appliedBeforeFailures = appliedCount;

    const auto config_path = config_root / "test_alg";
    const auto param_path  = config_path / "param.json";
    std::error_code error;
    REQUIRE(std::filesystem::remove(param_path, error));
    REQUIRE_FALSE(error);
    REQUIRE(std::filesystem::create_directory(param_path, error));
    REQUIRE_FALSE(error);

    MsgTaskArea newArea;
    newArea.areaId = "new-area";
    MsgTaskConfig rejected;
    rejected.params = {MakeParam("param.secondary", "7")};
    rejected.areas  = {newArea};
    REQUIRE(unit.SetChannelParams(rejected) == util::ErrorEnum::FileOpenFailed);

    CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "6");
    CHECK(FindParamValue(unit.GetParams(), "param.secondary") == "1");
    std::vector<MsgTaskArea> areas;
    std::vector<MsgTaskArea> shieldedAreas;
    REQUIRE(unit.GetArea(areas, shieldedAreas) == util::ErrorEnum::Success);
    REQUIRE(areas.size() == 1);
    CHECK(areas.front().areaId == "old-area");
    REQUIRE(shieldedAreas.size() == 1);
    CHECK(shieldedAreas.front().areaId == "old-shielded-area");

    CameraTaskUnitArea persistedArea;
    REQUIRE(util::LoadStructFromJsonFile((config_path / "area.json").string(), persistedArea));
    REQUIRE(persistedArea.areas.size() == 1);
    CHECK(persistedArea.areas.front().areaId == "old-area");
    REQUIRE(persistedArea.shieldedAreas.size() == 1);
    CHECK(persistedArea.shieldedAreas.front().areaId == "old-shielded-area");
    CHECK(unit.ApplyLatestTaskConfig());
    CHECK(appliedCount == appliedBeforeFailures);

    REQUIRE(unit.SetChannelParams(std::vector<MsgDynamicKeyValue>{MakeParam("param.secondary", "8")}) ==
            util::ErrorEnum::FileOpenFailed);
    CHECK(FindParamValue(unit.GetParams(), "param.threshold") == "6");
    CHECK(FindParamValue(unit.GetParams(), "param.secondary") == "1");
    CHECK(unit.ApplyLatestTaskConfig());
    CHECK(appliedCount == appliedBeforeFailures);

    REQUIRE(std::filesystem::remove(param_path, error));
    REQUIRE_FALSE(error);
    REQUIRE(unit.SetChannelParams(std::vector<MsgDynamicKeyValue>{MakeParam("param.threshold", "9")}) ==
            util::ErrorEnum::Success);
    const auto restored = LoadSavedParams(config_root, "test_alg");
    CHECK(FindParamValue(restored.params, "param.threshold") == "9");
    CHECK(FindParamValue(restored.params, "param.secondary") == "1");
    CHECK(HasOverrideKey(restored, "param.threshold"));
    CHECK_FALSE(HasOverrideKey(restored, "param.secondary"));
}
