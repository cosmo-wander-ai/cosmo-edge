#include <chrono>
#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "mock/MockAlgorithmService.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockCameraService.h"
#include "service/model/impl/ModelServiceImpl.h"
#include "support/MockDefaults.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "util/FileUtil.h"
#include "util/NnBackendConstants.h"
#include "util/PathUtil.h"

using namespace cosmo::service;
using namespace cosmo;

namespace {

struct ModelServiceDependencies {
    cosmo::test::MockAlgorithmService algSvc;
    cosmo::test::MockCameraService cameraSvc;
    cosmo::test::MockAppInfoService appInfoSvc;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::ScopedServiceOverride<IAlgorithmQuery> algorithmQuery{algSvc};
    cosmo::test::ScopedServiceOverride<ICameraTaskConfig> cameraTaskConfig{cameraSvc};
    cosmo::test::ScopedServiceOverride<IAppInfoService> appInfo{appInfoSvc};

    ModelServiceDependencies() {
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetModelDebug()).RETURN(false));
    }
};

}  // namespace

// Helper: create a minimal model directory with config.json on disk
static std::string CreateTestModelOnDisk(const std::string& modelsDir, const std::string& modelCode,
                                         const std::string& modelName,
                                         const std::string& dirPrefix = cosmo::util::kPlatformDirPrefix,
                                         const std::string& chipType  = "") {
    std::string dirName  = std::string(dirPrefix) + modelCode + "_" + modelName + "_V1.0.0";
    std::string modelDir = (std::filesystem::path(modelsDir) / dirName).string();
    std::filesystem::create_directories(modelDir);

    std::string configJson = R"({
        "algorithm_code": ")" +
                             modelCode + R"(",
        "chip_type": ")" + chipType +
                             R"(",
        "model_type": "yolov8_det",
        "version": "1.0",
        "models": [{"name": ")" +
                             modelName + R"(", "inputs": [], "outputs": []}],
        "labels": [
            {"id": "0", "name": "person", "threshold": [0.8, 0.5]},
            {"id": "1", "name": "car", "threshold": [0.7, 0.4]}
        ]
    })";

    std::ofstream ofs(modelDir + "/config.json");
    ofs << configJson;
    ofs.close();
    return modelDir;
}

TEST_CASE("ModelServiceImpl: 模型服务核心逻辑", "[model-service]") {
    // 设置测试专用的隔离目录
    std::string testBaseDir =
        "/tmp/cosmo_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    // Redirect path roots to test directory
    // Separate user (data) and preset (app) roots so preset-path checks are testable.
    cosmo::test::ScopedPathOverride path_override(testBaseDir + "/udata", testBaseDir + "/adata");
    ModelServiceDependencies mocks;
    // GetModelPath()         -> udata/resource/models  (user models: exportable/deletable)
    // GetPresetModelPath()   -> adata/resource/models  (preset models: protected)
    // GetModelTemplatePath() -> adata/resource/model_template
    auto testModelsDir      = cosmo::path::GetModelPath();
    auto testPresetDir      = cosmo::path::GetPresetModelPath();
    auto testTemplatePath   = cosmo::path::GetModelTemplatePath();
    auto testComponentsPath = cosmo::path::GetModelComponentsJsonPath();
    ModelServiceImpl sut;

    SECTION("路径获取测试") {
        REQUIRE(sut.GetModelPath() == testModelsDir);
        REQUIRE(sut.GetModelTemplatePath() == testTemplatePath);
        REQUIRE(sut.GetModelComponentsJsonPath() == testComponentsPath);
    }

    SECTION("ModelValid 检查无效模型应返回 false") {
        REQUIRE(sut.ModelValid("invalid_model") == false);

        std::string name;
        REQUIRE(sut.ModelValid("invalid_model", name) == false);
        REQUIRE(name == "invalid_model");  // 如果没找到，name 会被赋值为 code
    }

    SECTION("legacy model import rejects an archive with no listed entries") {
        const auto archive_path = std::filesystem::path(cosmo::path::GetUploadPath()) / "empty.zip";
        std::string end_of_central_directory(22, '\0');
        end_of_central_directory[0] = 'P';
        end_of_central_directory[1] = 'K';
        end_of_central_directory[2] = '\x05';
        end_of_central_directory[3] = '\x06';
        std::ofstream archive(archive_path, std::ios::binary);
        archive.write(end_of_central_directory.data(),
                      static_cast<std::streamsize>(end_of_central_directory.size()));
        archive.close();

        REQUIRE(sut.ModelAdd(archive_path.string()) == cosmo::util::ErrorEnum::UnZipFileFailed);
        REQUIRE(std::filesystem::exists(archive_path));
    }

    SECTION("preset model config stays editable and retains its factory snapshot") {
        CreateTestModelOnDisk(testPresetDir, "preset_cfg_001", "PresetModel");
        sut.Init();

        std::string config_json;
        std::string default_config_json;
        bool is_exportable{true};
        REQUIRE(sut.GetModelConfig("preset_cfg_001", config_json, is_exportable, default_config_json) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE_FALSE(is_exportable);
        REQUIRE(config_json.find("PresetModel") != std::string::npos);
        REQUIRE(default_config_json == config_json);

        ALLOW_CALL(mocks.algSvc, GetAlgorithmsByModelId("preset_cfg_001")).RETURN(std::vector<std::string>{});
        std::string edited_config   = config_json;
        const size_t model_name_pos = edited_config.find("PresetModel");
        REQUIRE(model_name_pos != std::string::npos);
        edited_config.replace(model_name_pos, std::string("PresetModel").size(), "EditedPreset");
        REQUIRE(sut.SaveModelConfig("preset_cfg_001", edited_config) == cosmo::util::ErrorEnum::Success);

        sut.Init();
        is_exportable       = true;
        default_config_json = "sentinel";
        REQUIRE(sut.GetModelConfig("preset_cfg_001", config_json, is_exportable, default_config_json) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE_FALSE(is_exportable);
        REQUIRE(config_json.find("EditedPreset") != std::string::npos);
        REQUIRE(default_config_json.find("PresetModel") != std::string::npos);
        REQUIRE(default_config_json.find("EditedPreset") == std::string::npos);

        int total = 0;
        std::vector<cosmo::Model::MsgModel> rows;
        sut.QueryModels("", "preset_cfg_001", 1, 10, total, rows);
        REQUIRE(total == 1);
        REQUIRE(rows.size() == 1);
        REQUIRE_FALSE(rows[0].isExportable);
        REQUIRE(sut.DeleteModel("preset_cfg_001") == cosmo::util::ErrorEnum::DefaultCantBeDelete);
    }

    SECTION("磁盘模型查询测试") {
        // 在磁盘上创建测试模型目录
        const std::string userModelDir = CreateTestModelOnDisk(testModelsDir, "test_model_001", "TestModel");

        // 测试有效性校验
        REQUIRE(sut.ModelValid("test_model_001") == true);

        std::string foundName;
        REQUIRE(sut.ModelValid("test_model_001", foundName) == true);
        REQUIRE(foundName == "TestModel");

        // 测试信息获取
        auto info = sut.GetModelInfo("test_model_001");
        REQUIRE(info.id == "test_model_001");
        REQUIRE(info.name == "TestModel");
        REQUIRE(info.labels.size() == 2);
        REQUIRE(info.labels[0].code == "person");
        REQUIRE(info.labels[0].confidenceHigh == Catch::Approx(0.8f));
        REQUIRE(info.labels[0].confidence == Catch::Approx(0.5f));

        // 测试分页查询
        size_t total = 0;
        auto results = sut.QueryModelInfo("", "test_model_001", 1, 10, total);
        REQUIRE(total == 1);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].id == "test_model_001");

        SECTION("DeleteModel 非法参数与预置模型") {
            REQUIRE(sut.DeleteModel("") == cosmo::util::ErrorEnum::InvalidParam);

            // 预置（内置）模型不可删除 —— 在预置目录造一个
            const std::string presetModelDir =
                CreateTestModelOnDisk(testPresetDir, "preset_del_001", "PresetDelModel");
            REQUIRE(sut.DeleteModel("preset_del_001") == cosmo::util::ErrorEnum::DefaultCantBeDelete);
            REQUIRE(std::filesystem::is_regular_file(std::filesystem::path(presetModelDir) / "config.json"));

            // 可以删除我们创建的用户模型 test_model_001
            ALLOW_CALL(mocks.algSvc, GetAlgorithmsByModelId("test_model_001"))
                .RETURN(std::vector<std::string>{});
            ALLOW_CALL(mocks.cameraSvc, NotifyAlgorithmsChanged(trompeloeil::_, false));
            REQUIRE(sut.DeleteModel("test_model_001") == cosmo::util::ErrorEnum::Success);
            REQUIRE_FALSE(std::filesystem::exists(userModelDir));
            REQUIRE(std::filesystem::is_regular_file(std::filesystem::path(presetModelDir) / "config.json"));

            // 清理预置目录，避免残留污染后续 QueryModels 等 SECTION
            std::filesystem::remove_all(testPresetDir);
        }

        SECTION("UpdateModel 流程") {
            // UpdateModel 允许修改（含预置模型，config 改动可逆），这里验证用户模型可改
            ALLOW_CALL(mocks.algSvc, GetAlgorithmsByModelId("test_model_001"))
                .RETURN(std::vector<std::string>{});
            ALLOW_CALL(mocks.cameraSvc, NotifyAlgorithmsChanged(trompeloeil::_, true));
            REQUIRE(sut.UpdateModel("test_model_001", "NewTestName", 8, "New Desc") ==
                    cosmo::util::ErrorEnum::Success);

            // 验证修改
            auto info = sut.GetModelInfo("test_model_001");
            REQUIRE(info.name == "NewTestName");
        }

        SECTION("GetModelConfig / SaveModelConfig 流程") {
            std::string cfgJson;
            std::string defaultCfgJson;
            bool isExportable{false};
            REQUIRE(sut.GetModelConfig("test_model_001", cfgJson, isExportable, defaultCfgJson) ==
                    cosmo::util::ErrorEnum::Success);
            REQUIRE(cfgJson.find("TestModel") != std::string::npos);
            REQUIRE(isExportable);
            REQUIRE(defaultCfgJson.empty());

            ALLOW_CALL(mocks.algSvc, GetAlgorithmsByModelId("test_model_001"))
                .RETURN(std::vector<std::string>{});
            ALLOW_CALL(mocks.cameraSvc, NotifyAlgorithmsChanged(trompeloeil::_, true));
            // 修改并保存
            std::string newJson = cfgJson;
            // 简单替换 TestModel 为 SavedModel
            size_t pos = newJson.find("TestModel");
            if (pos != std::string::npos) {
                newJson.replace(pos, 9, "SavedModel");
            }
            REQUIRE(sut.SaveModelConfig("test_model_001", newJson) == cosmo::util::ErrorEnum::Success);

            std::string updatedJson;
            isExportable   = false;
            defaultCfgJson = "sentinel";
            REQUIRE(sut.GetModelConfig("test_model_001", updatedJson, isExportable, defaultCfgJson) ==
                    cosmo::util::ErrorEnum::Success);
            REQUIRE(updatedJson.find("SavedModel") != std::string::npos);
            REQUIRE(isExportable);
            REQUIRE(defaultCfgJson.empty());
        }

        SECTION("QueryModels 流程") {
            int total = 0;
            std::vector<cosmo::Model::MsgModel> rows;
            sut.QueryModels("", "", 1, 10, total, rows);
            REQUIRE(total == 1);
            REQUIRE(rows.size() == 1);
            REQUIRE(rows[0].modelCode == "test_model_001");
            REQUIRE(rows[0].modelName == "TestModel");
            REQUIRE(rows[0].isExportable);
        }

        SECTION("QueryAtomicModels 流程") {
            auto atomicRows = sut.QueryAtomicModels("", "", "");
            REQUIRE(atomicRows.size() == 1);
            REQUIRE(atomicRows[0].atomicCode == "test_model_001");
            REQUIRE(atomicRows[0].atomicName == "TestModel");
            REQUIRE(atomicRows[0].labelList.size() == 2);
        }

        SECTION("GetModelPathMapping 可以正确返回映射路径") {
            sut.SetModelPathMapping("test_model_001", testModelsDir + "/some_path");
            REQUIRE(sut.GetModelPathMapping("test_model_001") == testModelsDir + "/some_path");

            // 无效映射返回空字符串
            REQUIRE(sut.GetModelPathMapping("invalid_code") == "");
        }

        SECTION("OCR 模型包路径必须使用配置绑定的字符表") {
            const auto modelDir =
                std::filesystem::path(testModelsDir) /
                (std::string(cosmo::util::kPlatformDirPrefix) + "test_model_001_TestModel_V1.0.0");
            const auto modelFile = modelDir / ("model" + std::string(cosmo::util::kModelFileExt));
            const auto dictFile  = modelDir / "character_table.txt";
            std::ofstream(modelFile).close();
            {
                std::ofstream config(modelDir / "config.json");
                config
                    << R"({"algorithm_code":"test_model_001","model_type":"ocr","models":[{"name":"TestModel","params":{"character_table_file":"character_table.txt"}}]})";
            }
            {
                std::ofstream dict(dictFile);
                dict << "\nA\n";
            }
            sut.SetModelPathMapping("test_model_001", modelDir.string());

            std::string configPath;
            std::string resolvedModelPath;
            std::string dictionaryPath;
            REQUIRE(sut.GetModelCfg("test_model_001", configPath, resolvedModelPath, dictionaryPath));
            REQUIRE(configPath == (modelDir / "config.json").string());
            REQUIRE(resolvedModelPath == modelFile.string());
            REQUIRE(dictionaryPath == dictFile.string());

            std::filesystem::remove(dictFile);
            REQUIRE_FALSE(sut.GetModelCfg("test_model_001", configPath, resolvedModelPath, dictionaryPath));

            std::ofstream(dictFile).close();
            std::ofstream(modelDir / "duplicate.txt").close();
            REQUIRE(sut.GetModelCfg("test_model_001", configPath, resolvedModelPath, dictionaryPath));

            {
                std::ofstream config(modelDir / "config.json");
                config
                    << R"({"algorithm_code":"test_model_001","model_type":"ocr","models":[{"name":"TestModel","params":{"character_table_file":"../duplicate.txt"}}]})";
            }
            REQUIRE_FALSE(sut.GetModelCfg("test_model_001", configPath, resolvedModelPath, dictionaryPath));
        }

        SECTION("OCR 模型包缺少配置时路径解析失败") {
            const auto missingConfigDir = std::filesystem::path(testModelsDir) / "missing_config";
            std::filesystem::create_directories(missingConfigDir);
            std::ofstream(missingConfigDir / ("model" + std::string(cosmo::util::kModelFileExt))).close();
            std::ofstream(missingConfigDir / "dictionary.txt").close();
            sut.SetModelPathMapping("missing_config", missingConfigDir.string());

            std::string configPath;
            std::string resolvedModelPath;
            std::string dictionaryPath;
            REQUIRE_FALSE(sut.GetModelCfg("missing_config", configPath, resolvedModelPath, dictionaryPath));
        }
    }

    SECTION("CV186X 芯片目录可被芯片无关地发现") {
        // prod_CV186X_<code>_... 目录(芯片型号不同于 prod_BM1688_)也必须能被发现并加载
        CreateTestModelOnDisk(testModelsDir, "7002001", "Cv186xModel", "prod_CV186X_", "CV186X");

        REQUIRE(sut.ModelValid("7002001") == true);

        std::string foundName;
        REQUIRE(sut.ModelValid("7002001", foundName) == true);
        REQUIRE(foundName == "Cv186xModel");

        auto info = sut.GetModelInfo("7002001");
        REQUIRE(info.id == "7002001");
        REQUIRE(info.name == "Cv186xModel");
    }

    std::filesystem::remove_all(testBaseDir);
}
