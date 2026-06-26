#include <filesystem>
#include <fstream>
#include <functional>

#include "util/PathUtil.h"
#include "util/PlatformConstants.h"

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "nlohmann/json.hpp"
#ifdef COSMO_NN_USE_CPU_BACKEND
#include "nn/core/shared_resource.h"
#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/device/cpu/cpu_normalize_node.h"
#include "nn/utils/op.h"
#endif
#define private public
#include "service/model/impl/ModelImportExporter.h"
#undef private
#include "mock/MockServiceRegistry.h"
#include "util/JsonFileUtil.h"

using namespace cosmo::service;
using namespace cosmo::test;
namespace fs = std::filesystem;

TEST_CASE("ModelImportExporter Tests", "[model]") {
    MockServiceRegistry mocks;
    ModelImportExporter importExporter;

    std::string testRoot = "/tmp/cosmo_test_models";
    cosmo::path::OverrideRootPathForTest(testRoot, testRoot);

    std::string testModelDir    = testRoot + "/resource/models";
    std::string testTemplateDir = testRoot + "/resource/model_template";
    fs::create_directories(testModelDir);
    fs::create_directories(testTemplateDir);

    std::ofstream outTemp(testTemplateDir + "/DET.json");
    outTemp
        << "{\"version\":\"1.0\",\"algorithm_code\":\"\",\"models\":[{\"name\":\"\",\"description\":\"\"}]}";
    outTemp.close();

    std::ofstream outTemp2(testTemplateDir + "/qwen3vl.json");
    outTemp2
        << "{\"version\":\"1.0\",\"algorithm_code\":\"\",\"models\":[{\"name\":\"\",\"description\":\"\"}]}";
    outTemp2.close();

    // Setup helper methods
    importExporter.SetHelpers([&]() { return testModelDir; }, [&]() { return testTemplateDir; },
                              [&](const std::string& code) {
                                  if (code == "1234567")
                                      return testModelDir + "/1234567";
                                  if (code == "test_export_model")
                                      return testModelDir + "/test_export_model";
                                  return std::string("");
                              },
                              [&]() { return std::string("1234567"); }, [&](const nlohmann::json&) {},
                              [&](const std::string&, const std::string&) {});

    SECTION("2.1 AddAtomicModel：验证目录创建、config.json 生成、model.nn 创建") {
        std::string bmodelSrc = "/tmp/cosmo_test_models/source.bmodel";
        std::ofstream out(bmodelSrc);
        out << "fake bmodel";
        out.close();

        std::string tokenizerSrc = "/tmp/cosmo_test_models/tokenizer.json";
        std::ofstream outTok(tokenizerSrc);
        outTok << "{}";
        outTok.close();

        std::vector<cosmo::Model::BmodelFileInfo> bmodelFiles;
        cosmo::Model::BmodelFileInfo info;
        info.filePath = bmodelSrc;
        info.role     = "BM1684X";
        bmodelFiles.push_back(info);

        auto res = importExporter.AddAtomicModel("new_atomic_model", "AtomicName", "qwen3vl", "desc",
                                                 bmodelFiles, "", tokenizerSrc, "", "");
        REQUIRE(res == cosmo::util::ErrorEnum::Success);

        std::string expectedDir =
            testModelDir + "/" + std::string(cosmo::util::kPlatformDirPrefix) + "1234567_AtomicName_V1.0.0";
        REQUIRE(fs::exists(expectedDir));
        REQUIRE(fs::exists(expectedDir + "/config.json"));
        REQUIRE(fs::exists(expectedDir + "/model" + std::string(cosmo::util::kModelFileExt)));

        nlohmann::json doc;
        REQUIRE(cosmo::util::JsonFileUtil::ReadJsonFile(expectedDir + "/config.json", doc) ==
                cosmo::util::ErrorEnum::Success);
        REQUIRE(doc.contains("algorithm_code"));
        REQUIRE(doc["algorithm_code"].get<std::string>() == "1234567");

        fs::remove_all(expectedDir);
        fs::remove(bmodelSrc);
        fs::remove(tokenizerSrc);
    }

    SECTION("classify ONNX dynamic batch keeps concrete class count") {
        nlohmann::json doc = {
            {"algorithm_code", ""},
            {"version", ""},
            {"model_type", "classify"},
            {"models",
             {{{"name", ""},
               {"description", ""},
               {"max_batch", 1},
               {"inputs", {{{"name", "input"}, {"data_type", 0}, {"shape", {1, 3, 224, 224}}}}},
               {"outputs", {{{"name", "output"}, {"data_type", 0}, {"shape", {1, 1000}}}}}}}}};

        cosmo::BmodelInfo bmodelInfo;
        bmodelInfo.valid = true;
        cosmo::BmodelNetworkInfo network;
        network.name      = "onnx_network";
        network.max_batch = 1;
        network.inputs.push_back({"input", {-1, 3, 224, 224}, 0});
        network.outputs.push_back({"output", {-1, 2}, 0});
        bmodelInfo.networks.push_back(network);

        importExporter.UpdateTemplateConfig(doc, "4208523", "V1.0.0", "falldown", "classify", "desc",
                                            {bmodelInfo}, false, "0-1", "bgr");

        REQUIRE(doc["models"][0]["outputs"][0]["shape"].size() == 2);
        REQUIRE(doc["models"][0]["outputs"][0]["shape"][0].get<int>() == 1);
        REQUIRE(doc["models"][0]["outputs"][0]["shape"][1].get<int>() == 2);
        REQUIRE(doc["labels"].size() == 2);
        REQUIRE(doc["labels"][0]["name"].get<std::string>() == "category0");
        REQUIRE(doc["labels"][1]["name"].get<std::string>() == "category1");
    }

    SECTION("yolov5 ONNX raw head outputs expand single-output template") {
        nlohmann::json doc = {
            {"algorithm_code", ""},
            {"version", ""},
            {"model_type", "yolov5_det"},
            {"models",
             {{{"name", ""},
               {"description", ""},
               {"max_batch", 1},
               {"inputs", {{{"name", "images"}, {"data_type", 0}, {"shape", {1, 3, 640, 640}}}}},
               {"outputs", {{{"name", "output0"}, {"data_type", 0}, {"shape", {1, 25200, 85}}}}},
               {"params",
                {{"input_size", {640, 640}},
                 {"confidence_threshold", 0.1},
                 {"nms_threshold", 0.35},
                 {"top_k", 1000}}}}}}};

        cosmo::BmodelInfo bmodelInfo;
        bmodelInfo.valid = true;
        cosmo::BmodelNetworkInfo network;
        network.name      = "onnx_network";
        network.max_batch = 1;
        network.inputs.push_back({"images", {1, 3, 640, 640}, 0});
        network.outputs.push_back({"head_small", {1, 3, 80, 80, 6}, 0});
        network.outputs.push_back({"head_medium", {1, 3, 40, 40, 6}, 0});
        network.outputs.push_back({"head_large", {1, 3, 20, 20, 6}, 0});
        bmodelInfo.networks.push_back(network);

        importExporter.UpdateTemplateConfig(doc, "4208523", "V1.0.0", "pedestrian", "yolov5_det", "desc",
                                            {bmodelInfo}, false, "0-1", "rgb");

        const auto& model = doc["models"][0];
        REQUIRE(model["outputs"].size() == 3);
        REQUIRE(model["outputs"][0]["name"].get<std::string>() == "head_small");
        REQUIRE(model["outputs"][1]["shape"][2].get<int>() == 40);
        REQUIRE(model["outputs"][2]["shape"][3].get<int>() == 20);
        REQUIRE(model["params"]["use_npu_postprocess"].get<bool>());
        REQUIRE(model["params"]["anchors"].size() == 3);
        REQUIRE(model["params"]["stride"].size() == 3);
        REQUIRE(doc["labels"][0]["threshold"][0].get<double>() == 0.25);
        REQUIRE(doc["labels"][0]["threshold"][1].get<double>() == 0.25);
    }

    SECTION("yolov5 ONNX dynamic single output does not inherit template output shape") {
        nlohmann::json doc = {
            {"algorithm_code", ""},
            {"version", ""},
            {"model_type", "yolov5_det"},
            {"models",
             {{{"name", ""},
               {"description", ""},
               {"max_batch", 1},
               {"inputs", {{{"name", "images"}, {"data_type", 0}, {"shape", {1, 3, 640, 640}}}}},
               {"outputs", {{{"name", "output0"}, {"data_type", 0}, {"shape", {1, 25200, 85}}}}},
               {"params",
                {{"input_size", {640, 640}},
                 {"confidence_threshold", 0.1},
                 {"nms_threshold", 0.35},
                 {"top_k", 1000}}}}}}};

        cosmo::BmodelInfo bmodelInfo;
        bmodelInfo.valid = true;
        cosmo::BmodelNetworkInfo network;
        network.name      = "onnx_network";
        network.max_batch = 1;
        network.inputs.push_back({"images", {-1, 3, 512, 512}, 0});
        network.outputs.push_back({"output", {-1, -1, -1}, 0});
        bmodelInfo.networks.push_back(network);

        importExporter.UpdateTemplateConfig(doc, "4208523", "V1.0.0", "face", "yolov5_det", "desc",
                                            {bmodelInfo}, false, "0-1", "rgb");

        const auto& model = doc["models"][0];
        REQUIRE(model["inputs"][0]["shape"][0].get<int>() == 1);
        REQUIRE(model["inputs"][0]["shape"][2].get<int>() == 512);
        REQUIRE(model["params"]["input_size"][0].get<int>() == 512);
        REQUIRE(model["outputs"][0]["shape"][0].get<int>() == 1);
        REQUIRE(model["outputs"][0]["shape"][1].get<int>() == -1);
        REQUIRE(model["outputs"][0]["shape"][2].get<int>() == -1);
    }

    SECTION("2.2 ImportModel（tar.gz）：验证解压、目录搬迁") {
        // We will create a fake tar.gz. Wait, without a real tar.gz we can't test tar.gz extraction easily
        // unless we use (void)!system("tar -czf ...").
        // Let's create a real tar.gz using system.
        std::string tempArchiveDir = "/tmp/cosmo_test_archive_dir";
        fs::create_directories(tempArchiveDir + "/fake_model");
        std::ofstream outCfg(tempArchiveDir + "/fake_model/config.json");
        outCfg << "{\"modelCode\": \"fake_model\", \"algorithmVersion\": \"1.0.0\"}";
        outCfg.close();

        std::string modelFile = "model" + std::string(cosmo::util::kModelFileExt);
        std::ofstream outNn(tempArchiveDir + "/fake_model/" + modelFile);
        outNn << "fake";
        outNn.close();

        std::string tarFile = "/tmp/cosmo_test_models/test_import.tar.gz";
        std::string cmd     = "cd " + tempArchiveDir + " && tar -czf " + tarFile + " fake_model";
        (void)!system(cmd.c_str());

        auto res = importExporter.ImportModel(tarFile);
        REQUIRE(res == cosmo::util::ErrorEnum::Success);
        REQUIRE(fs::exists(testModelDir + "/fake_model/config.json"));

        fs::remove_all(tempArchiveDir);
        fs::remove_all(testModelDir + "/fake_model");
        fs::remove(tarFile);
    }

    SECTION("2.3 ImportModel（flat 结构）：验证自动包装为标准目录") {
        // Create a flat tar.gz where config.json is at the root of the archive
        std::string tempArchiveDir = "/tmp/cosmo_test_flat_archive_dir";
        fs::create_directories(tempArchiveDir);
        std::ofstream outCfg(tempArchiveDir + "/config.json");
        outCfg << "{\"modelCode\": \"flat_model\", \"algorithmVersion\": \"1.0.0\"}";
        outCfg.close();

        std::string modelFile = "model" + std::string(cosmo::util::kModelFileExt);
        std::ofstream outNn(tempArchiveDir + "/" + modelFile);
        outNn << "fake";
        outNn.close();

        std::string tarFile = "/tmp/cosmo_test_models/flat_import.tar.gz";
        std::string cmd = "cd " + tempArchiveDir + " && tar -czf " + tarFile + " config.json " + modelFile;
        (void)!system(cmd.c_str());

        auto res = importExporter.ImportModel(tarFile);
        REQUIRE(res == cosmo::util::ErrorEnum::Success);

        // ImportFlatArchive uses kPlatformDirPrefix + alg_code + "_" + name + "_" + version
        // alg_code defaults to "0000000" (no "algorithm_code" key in config.json),
        // name defaults to "imported", version defaults to "V1.0.0"
        REQUIRE(fs::exists(testModelDir + "/" + std::string(cosmo::util::kPlatformDirPrefix) +
                           "0000000_imported_V1.0.0/config.json"));

        fs::remove_all(tempArchiveDir);
        fs::remove_all(testModelDir + "/" + std::string(cosmo::util::kPlatformDirPrefix) +
                       "0000000_imported_V1.0.0");
        fs::remove(tarFile);
    }

    SECTION("2.4 ExportModelConfig：验证 tar.gz 创建") {
        std::string exportModelDir = testModelDir + "/test_export_model";
        fs::create_directories(exportModelDir);
        std::ofstream outCfg(exportModelDir + "/config.json");
        outCfg << "{\"modelCode\": \"test_export_model\", \"algorithmVersion\": \"1.0.0\"}";
        outCfg.close();

        std::string outPath;
        std::string outName;
        auto res = importExporter.ExportModelConfig("test_export_model", "export_name", outPath, outName);

        REQUIRE(res == cosmo::util::ErrorEnum::Success);
        REQUIRE(fs::exists(outPath));
        REQUIRE(outName.find("test_export_model") != std::string::npos);

        fs::remove_all(exportModelDir);
        fs::remove(outPath);
    }

    fs::remove_all(testModelDir);
    fs::remove_all(testTemplateDir);
}

#ifdef COSMO_NN_USE_CPU_BACKEND
TEST_CASE("CPU classify crop preprocess keeps normalize shape non-zero", "[cwnn][cpu]") {
    cosmo::nn::SharedResource shared;

    cosmo::nn::CropResize crop_op("crop_resize");
    crop_op.type          = "crop";
    crop_op.h_top_crop    = {0.0f};
    crop_op.h_bottom_crop = {0.0f};
    crop_op.w_left_crop   = {0.0f};
    crop_op.w_right_crop  = {0.0f};
    crop_op.dsize         = {224, 224};
    crop_op.color         = {114, 114, 114};

    cosmo::nn::CpuCropResizeNode crop;
    crop.SetMaxBatch(1);
    crop.SetSharedResource(&shared);
    crop.LoadParam(&crop_op);
    REQUIRE(bool(crop.InferTopShapes()));
    REQUIRE(shared.net_input_h == 224);
    REQUIRE(shared.net_input_w == 224);

    cosmo::nn::Normalize normalize_op("normalize");
    normalize_op.mean  = {0.0f, 0.0f, 0.0f};
    normalize_op.scale = 0.00392157f;

    cosmo::nn::CpuNormalizeNode normalize;
    normalize.SetMaxBatch(1);
    normalize.SetSharedResource(&shared);
    normalize.LoadParam(&normalize_op);
    REQUIRE(bool(normalize.InferTopShapesWithBottoms({{1, 224, 224, 3}}, {cosmo::nn::DATA_TYPE_UINT8})));

    auto top_shapes = normalize.GetTopBlobShapes();
    const cosmo::nn::DimsVector expected_shape{1, 3, 224, 224};
    REQUIRE(top_shapes.size() == 1);
    REQUIRE(top_shapes[0] == expected_shape);
}
#endif
