#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "flow/detect/AiDetectMng.h"
#include "mock/MockModelService.h"
#include "nlohmann/json.hpp"
#include "nn/pipeline/pipeline_utils.h"
#include "support/ScopedServiceOverride.h"
#include "util/InferenceInputSize.h"
#include "util/UuidUtil.h"

using namespace cosmo;

namespace {
nlohmann::json ProfileConfig() {
    return {{"model_type", "yolo26_det"},
            {"models",
             {{{"max_batch", 1},
               {"inputs", {{{"name", "images"}, {"shape", {1, 3, 640, 640}}}}},
               {"outputs", {{{"shape", {1, 84, 8400}}}}},
               {"params",
                {{"input_size", {640, 640}}, {"input_sizes", {{320, 320}, {640, 640}, {384, 640}}}}}}}}};
}
}  // namespace

TEST_CASE("Inference sizes preserve H/W and reject undeclared selections", "[nn][input-sizes]") {
    InferenceInputSizes sizes;
    std::string error;
    REQUIRE(ReadInferenceInputSizes(ProfileConfig(), sizes, error));
    REQUIRE(sizes.sizes.size() == 3);
    CHECK(sizes.sizes[2] == InferenceInputSize{640, 384});
    InferenceInputSize selected;
    REQUIRE(ResolveInferenceInputSize(sizes, std::nullopt, selected, error));
    CHECK(selected == InferenceInputSize{640, 640});
    REQUIRE(ResolveInferenceInputSize(sizes, InferenceInputSize{640, 384}, selected, error));
    CHECK_FALSE(ResolveInferenceInputSize(sizes, InferenceInputSize{384, 640}, selected, error));
    CHECK_FALSE(ResolveInferenceInputSize(sizes, InferenceInputSize{512, 512}, selected, error));
    REQUIRE(ParseInferenceInputSize("640x384", selected));
    CHECK(selected == InferenceInputSize{640, 384});
    for (const auto* value : {"640", "640x0", "0640x384", "640x384x1", "640X384", "-1x320", "640x384 "})
        CHECK_FALSE(ParseInferenceInputSize(value, selected));
}

TEST_CASE("YOLO26 accepts exported profiles whose input heights are not stride aligned",
          "[nn][input-sizes]") {
    auto root                      = ProfileConfig();
    const auto* type               = GENERATE("yolo26_det", "yolo26_person_attributes");
    root["model_type"]             = type;
    auto& model                    = root["models"][0];
    model["inputs"][0]["shape"]    = {1, 3, 720, 1280};
    model["params"]["input_size"]  = {720, 1280};
    model["params"]["input_sizes"] = {{720, 1280}, {540, 960}, {360, 640}};
    if (root["model_type"] == "yolo26_person_attributes") {
        model["outputs"][0]["shape"] = {1, 300, 10};
        root["attributes"]           = {"smoking", "sleep", "fall", "climbing"};
    }

    nn::PipelineConfig config;
    REQUIRE(bool(nn::pipeline_utils::ParsePipelineConfig(root.dump(), config)));
    REQUIRE(config.input_sizes.sizes.size() == 3);
    CHECK(config.input_sizes.default_size == InferenceInputSize{1280, 720});
    CHECK(config.input_sizes.sizes[1] == InferenceInputSize{960, 540});
    CHECK(config.input_sizes.sizes[2] == InferenceInputSize{640, 360});
}

TEST_CASE("Invalid model size declarations fail pipeline parsing", "[nn][input-sizes]") {
    auto root          = ProfileConfig();
    const auto invalid = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    auto& model        = root["models"][0];
    if (invalid == 0)
        model["params"]["input_sizes"] = {{320, 320}, {640, 640}};
    if (invalid == 1)
        model["params"]["input_sizes"][0] = {640, 640};
    if (invalid == 2)
        model["params"]["input_sizes"][0] = {0, 320};
    if (invalid == 3)
        model["params"]["input_size"] = {512, 512};
    if (invalid == 4)
        model["inputs"][0]["shape"] = {1, 3, 320, 320};
    if (invalid == 5)
        model["max_batch"] = 2;
    if (invalid == 6)
        root["model_type"] = "yolov8_det";
    if (invalid == 7)
        model["max_batch"] = "1";
    if (invalid == 8)
        root["model_type"] = 26;
    InferenceInputSizes sizes;
    std::string error;
    CHECK_FALSE(ReadInferenceInputSizes(root, sizes, error));
    nn::PipelineConfig config;
    CHECK_FALSE(bool(nn::pipeline_utils::ParsePipelineConfig(root.dump(), config)));
}

TEST_CASE("Legacy models retain a single configured input size", "[nn][input-sizes]") {
    auto root = ProfileConfig();
    root["models"][0]["params"].erase("input_sizes");
    InferenceInputSizes sizes;
    std::string error;
    REQUIRE(ReadInferenceInputSizes(root, sizes, error));
    REQUIRE(sizes.sizes.empty());
    InferenceInputSize effective;
    REQUIRE(ResolveInferenceInputSize(sizes, std::nullopt, effective, error));
    CHECK(effective == InferenceInputSize{640, 640});
    CHECK_FALSE(ResolveInferenceInputSize(sizes, InferenceInputSize{320, 320}, effective, error));
}

TEST_CASE("Detector placement isolates sizes on the same channel", "[input-sizes][detector-placement]") {
    using trompeloeil::_;
    struct ConfigFile {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("cosmo-input-sizes-" + util::GenerateUUID() + ".json");
        ~ConfigFile() {
            std::filesystem::remove(path);
        }
    } file;
    auto config                 = ProfileConfig();
    config["algorithm_code"]    = "4000998";
    config["models"][0]["name"] = "fixture";
    std::ofstream(file.path) << config;
    test::MockModelService models;
    test::ScopedServiceOverride<service::IModelPathMapping> registration{models};
    ALLOW_CALL(models, GetModelCfg("4000998", _, _))
        .LR_SIDE_EFFECT(_2 = file.path.string())
        .LR_SIDE_EFFECT(_3 = "unused")
        .RETURN(true);
    AiDetectMng manager;
    ActionNode action;
    action.atomicCode     = "4000998";
    action.actionName     = "Detector action";
    auto default_instance = manager.GetDetectorForChannel(action.atomicCode, "camera", "default", action);
    REQUIRE(default_instance);
    CHECK(default_instance->GetDetectorName() == "4000998");
    CHECK(static_cast<const AlgActionBase&>(*default_instance).GetName() == "Detector action");
    CHECK(action.input_size == InferenceInputSize{640, 640});
    auto same = action;
    CHECK(manager.GetDetectorForChannel(same.atomicCode, "camera", "same", same) == default_instance);
    auto small          = action;
    small.input_size    = InferenceInputSize{320, 320};
    auto small_instance = manager.GetDetectorForChannel(small.atomicCode, "camera", "small", small);
    REQUIRE(small_instance);
    CHECK(small_instance != default_instance);
    auto another = small;
    auto other_instance =
        manager.GetDetectorForChannel(another.atomicCode, "other-camera", "small-other", another);
    REQUIRE(other_instance);
    CHECK(other_instance->GetInputSize() == small.input_size);
    REQUIRE(manager.DeleteInst(default_instance, "camera", "default"));
    REQUIRE(manager.DeleteInst(default_instance, "camera", "same"));
    CHECK(small_instance->TaskExist("camera", "small"));
    REQUIRE(manager.DeleteInst(small_instance, "camera", "small"));
    REQUIRE(manager.DeleteInst(other_instance, "other-camera", "small-other"));
    auto invalid       = action;
    invalid.input_size = InferenceInputSize{512, 512};
    CHECK_FALSE(manager.GetDetectorForChannel(invalid.atomicCode, "camera", "invalid", invalid));
}
