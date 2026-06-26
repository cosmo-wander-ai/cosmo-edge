#include <chrono>
#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "mock/MockAlgorithmService.h"
#include "mock/MockCameraService.h"
#include "mock/MockServiceRegistry.h"
#include "service/model/impl/ModelServiceImpl.h"
#include "util/FileUtil.h"
#include "util/PathUtil.h"
#include "util/PlatformConstants.h"

using namespace cosmo::service;
using namespace cosmo;

// Helper: create a minimal model directory with config.json on disk
static void CreateTestModelOnDisk(const std::string& modelsDir, const std::string& modelCode,
                                  const std::string& modelName) {
    std::string dirName =
        std::string(cosmo::util::kPlatformDirPrefix) + modelCode + "_" + modelName + "_V1.0.0";
    std::string modelDir = (std::filesystem::path(modelsDir) / dirName).string();
    std::filesystem::create_directories(modelDir);

    std::string configJson = R"({
        "algorithm_code": ")" +
                             modelCode + R"(",
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
}

TEST_CASE("ModelServiceImpl: 模型服务核心逻辑", "[model-service]") {
    // 设置测试专用的隔离目录
    std::string testBaseDir =
        "/tmp/cosmo_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    cosmo::test::MockServiceRegistry mocks;
    // Redirect path roots to test directory
    cosmo::path::OverrideRootPathForTest(testBaseDir, testBaseDir);
    // GetModelPath() -> testBaseDir/resource/models
    // GetModelTemplatePath() -> testBaseDir/resource/model_template
    auto testModelsDir      = cosmo::path::GetModelPath();
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

    SECTION("磁盘模型查询测试") {
        // 在磁盘上创建测试模型目录
        CreateTestModelOnDisk(testModelsDir, "test_model_001", "TestModel");

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

        SECTION("DeleteModel 非法参数与内置模型") {
            REQUIRE(sut.DeleteModel("") == cosmo::util::ErrorEnum::InvalidParam);
            REQUIRE(sut.DeleteModel("1000001") == cosmo::util::ErrorEnum::DefaultCantBeDelete);

            // 可以删除我们创建的 test_model_001
            ALLOW_CALL(mocks.algSvc, GetAlgorithmsByModelId("test_model_001"))
                .RETURN(std::vector<std::string>{});
            ALLOW_CALL(mocks.cameraSvc, NotifyAlgorithmsChanged(trompeloeil::_, false));
            REQUIRE(sut.DeleteModel("test_model_001") == cosmo::util::ErrorEnum::Success);
            REQUIRE(std::filesystem::exists(testModelsDir + "/" +
                                            std::string(cosmo::util::kPlatformDirPrefix) +
                                            "test_model_001_TestModel_V1.0.0") == false);
        }

        SECTION("UpdateModel 流程") {
            // 不能更新内置模型
            REQUIRE(sut.UpdateModel("1000001", "new_name", 4, "desc") ==
                    cosmo::util::ErrorEnum::DefaultCantBeUpdate);

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
            REQUIRE(sut.GetModelConfig("test_model_001", cfgJson) == cosmo::util::ErrorEnum::Success);
            REQUIRE(cfgJson.find("TestModel") != std::string::npos);

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
            REQUIRE(sut.GetModelConfig("test_model_001", updatedJson) == cosmo::util::ErrorEnum::Success);
            REQUIRE(updatedJson.find("SavedModel") != std::string::npos);
        }

        SECTION("QueryModels 流程") {
            int total = 0;
            std::vector<cosmo::Model::MsgModel> rows;
            sut.QueryModels("", "", 1, 10, total, rows);
            REQUIRE(total == 1);
            REQUIRE(rows.size() == 1);
            REQUIRE(rows[0].modelCode == "test_model_001");
            REQUIRE(rows[0].modelName == "TestModel");
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
    }

    std::filesystem::remove_all(testBaseDir);
}
