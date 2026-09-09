#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/pipeline/pipeline_utils.h"
#include "nn/utils/model_info_utils.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "infer/AiClassifierUnify.h"
#include "infer/AiDetectorUnify.h"
#include "media/OsdTextRenderer.h"
#include "media/VideoFrameProcSophon.h"
#include "mem/DeviceContext.h"
#include "mem/IDeviceContext.h"
#include "service/infra/impl/MemoryPoolServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#endif

namespace cosmo::nn {
namespace {

    class ModelConfigFile {
    public:
        ModelConfigFile() {
            auto pattern = (std::filesystem::temp_directory_path() / "cosmo-model-config-XXXXXX").string();
            std::vector<char> name(pattern.begin(), pattern.end());
            name.push_back('\0');
            auto* directory = mkdtemp(name.data());
            REQUIRE(directory != nullptr);
            directory_ = directory;
        }

        ~ModelConfigFile() {
            std::error_code error;
            std::filesystem::remove_all(directory_, error);
        }

        std::filesystem::path Path() const {
            return directory_ / "config.json";
        }

        void Write(const std::string& content) const {
            std::ofstream file(Path(), std::ios::binary);
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            REQUIRE(file.good());
        }

    private:
        std::filesystem::path directory_;
    };

    TEST_CASE("Model config file loading preserves bytes and rejects invalid sizes", "[model-info-utils]") {
        ModelConfigFile file;
        std::string loaded = "previous";
        REQUIRE((ModelInfoUtils::LoadJson(file.Path().string(), loaded) == COSMO_NN_ERR_JSON_PARSE));
        REQUIRE(loaded == "previous");

        file.Write("");
        REQUIRE((ModelInfoUtils::LoadJson(file.Path().string(), loaded) == COSMO_NN_ERR_JSON_PARSE));

        const std::string content = "{\"name\":\"模型\\n标签\"}\n";
        file.Write(content);
        REQUIRE((ModelInfoUtils::LoadJson(file.Path().string(), loaded) == COSMO_NN_OK));
        REQUIRE(loaded == content);

        std::filesystem::resize_file(file.Path(), 10 * 1024 * 1024 + 1);
        REQUIRE((ModelInfoUtils::LoadJson(file.Path().string(), loaded) == COSMO_NN_ERR_JSON_PARSE));
        REQUIRE(loaded == content);
    }

    TEST_CASE("Model input shapes apply batch without changing empty inputs", "[model-info-utils]") {
        ModelInfo model;
        ShapesMap shapes{{"previous", {7}}};
        REQUIRE((ModelInfoUtils::GetInputShapesMap(model, shapes) == COSMO_NN_ERR_JSON_INVALID_INPUT));
        REQUIRE(shapes.at("previous") == DimsVector{7});
        model.input_node_infos.resize(2);
        model.input_node_infos[0].name  = "images";
        model.input_node_infos[0].shape = {-1, 3, 640, 640};
        model.input_node_infos[1].name  = "decoder";
        REQUIRE((ModelInfoUtils::GetInputShapesMap(model, shapes) == COSMO_NN_ERR_JSON_INVALID_BATCH));
        REQUIRE(shapes.count("previous") == 1);
        model.max_batch = 4;
        REQUIRE((ModelInfoUtils::GetInputShapesMap(model, shapes) == COSMO_NN_OK));
        REQUIRE(shapes.size() == 2);
        REQUIRE(shapes.at("images") == DimsVector{4, 3, 640, 640});
        REQUIRE(shapes.at("decoder").empty());
        REQUIRE(model.input_node_infos[0].shape == DimsVector{-1, 3, 640, 640});
    }

    TEST_CASE("Model output selection preserves node order and threshold early returns",
              "[model-info-utils]") {
        CombinedModelInfo model;
        std::vector<std::vector<float>> thresholds{{9.0f}};
        ModelInfoUtils::GetSelectedThreshold(model, thresholds);
        REQUIRE(thresholds == std::vector<std::vector<float>>{{9.0f}});
        model.config.instructions.resize(2);
        ModelInfoUtils::GetSelectedThreshold(model, thresholds);
        REQUIRE(thresholds == std::vector<std::vector<float>>{{9.0f}});
        model.config.instructions[0].infos = {{"7", "行人", {0.25f, 0.5f}}, {"2", "vehicle", {0.5f, 0.75f}}};
        model.config.instructions[1].infos = {{"11", "fire", {0.75f, 1.0f}}};
        for (int index : {-1, 2}) {
            ModelInfoUtils::GetSelectedThreshold(model, thresholds, index);
            REQUIRE(thresholds == std::vector<std::vector<float>>{{9.0f}});
        }
        ModelInfoUtils::GetSelectedThreshold(model, thresholds, 1);
        REQUIRE(thresholds == std::vector<std::vector<float>>{{0.5f, 0.75f}, {1.0f}});
        ModelInfoUtils::GetSelectedThreshold(model, thresholds);
        REQUIRE(thresholds == std::vector<std::vector<float>>{{0.25f, 0.5f}, {0.75f}});

        std::vector<std::vector<std::string>> names{{"previous"}};
        std::vector<int> indices{99};
        ModelInfoUtils::GetSelectedClassName(model, names);
        ModelInfoUtils::GetSelectedIndex(model, indices);
        REQUIRE(names == std::vector<std::vector<std::string>>{{"行人", "vehicle"}, {"fire"}});
        REQUIRE(indices == std::vector<int>{7, 2, 11});
        model.config.instructions.clear();
        ModelInfoUtils::GetSelectedClassName(model, names);
        ModelInfoUtils::GetSelectedIndex(model, indices);
        REQUIRE(names.empty());
        REQUIRE(indices.empty());
    }

    TEST_CASE("Pipeline config loads multiple models and retains current defaults", "[pipeline-config]") {
        const std::string source = R"({
        "model_type":"sam2", "chip_type":"BM1688", "algorithm_code":"42",
        "version":"V2", "reduce":"fixture", "models":[
            {"name":"编码器", "file_name":"encoder.nn", "file_md5":"fixture-digest", "max_batch":4,
             "inputs":[{"name":"images", "shape":[-1,3,640,640], "data_type":1}],
             "outputs":[{"name":"features", "shape":[4,256], "data_type":0}],
             "params":{"normalize_scale":0.5}},
            {"name":"decoder", "inputs":[{"name":"prompt", "shape":[]}]}],
        "labels":[{"id":"7", "name":"目标", "threshold":[0.25,0.5]}],
        "config":{"prompt_type":"point"}})";
        PipelineConfig config;
        REQUIRE((pipeline_utils::ParsePipelineConfig(source, config) == COSMO_NN_OK));
        REQUIRE(config.model_type == "sam2");
        REQUIRE(config.chip_type == "BM1688");
        REQUIRE(config.algorithm_code == "42");
        REQUIRE(config.version == "V2");
        REQUIRE(config.reduce == "fixture");
        REQUIRE(config.models.size() == 2);
        const auto& encoder = config.models[0];
        REQUIRE(encoder.name == "编码器");
        REQUIRE(encoder.file_name == "encoder.nn");
        REQUIRE(encoder.file_md5 == "fixture-digest");
        REQUIRE(encoder.max_batch == 4);
        REQUIRE(encoder.inputs.size() == 1);
        REQUIRE(encoder.inputs[0].name == "images");
        REQUIRE(encoder.inputs[0].shape == DimsVector{-1, 3, 640, 640});
        REQUIRE(encoder.inputs[0].data_type == 1);
        REQUIRE(encoder.outputs.size() == 1);
        REQUIRE(encoder.outputs[0].name == "features");
        REQUIRE(encoder.outputs[0].shape == DimsVector{4, 256});
        REQUIRE(encoder.params_json == R"({"normalize_scale":0.5})");
        REQUIRE(config.models[1].name == "decoder");
        REQUIRE(config.models[1].max_batch == 1);
        REQUIRE(config.models[1].inputs.size() == 1);
        REQUIRE(config.models[1].inputs[0].shape.empty());
        REQUIRE(config.labels.size() == 1);
        REQUIRE(config.labels[0].id == "7");
        REQUIRE(config.labels[0].name == "目标");
        REQUIRE(config.labels[0].threshold == std::vector<float>{0.25f, 0.5f});
        REQUIRE(config.extra_config_json == R"({"prompt_type":"point"})");

        PipelineConfig minimal;
        REQUIRE((pipeline_utils::ParsePipelineConfig(R"({"model_type":"classify","models":[{}]})", minimal) ==
                 COSMO_NN_OK));
        REQUIRE(minimal.models.size() == 1);
        REQUIRE(minimal.chip_type == "sophon");
        REQUIRE(minimal.version == "V1");
        REQUIRE(minimal.labels.empty());
    }

    TEST_CASE("Pipeline config rejects malformed or incomplete current configurations", "[pipeline-config]") {
        for (const auto& source :
             {"{", "{}", R"({"model_type":"classify"})", R"({"model_type":"classify","models":[]})"}) {
            CAPTURE(source);
            PipelineConfig config;
            REQUIRE((pipeline_utils::ParsePipelineConfig(source, config) == COSMO_NN_ERR_JSON_PARSE));
        }
    }

    TEST_CASE("Current resource model templates and configs use the pipeline parser",
              "[.model-config-resources]") {
        const auto* resource_dir = std::getenv("COSMO_TEST_MODEL_RESOURCE_DIR");
        REQUIRE(resource_dir != nullptr);
        const std::filesystem::path root(resource_dir);
        for (const auto& subdir : {"model_template", "models"}) {
            const auto directory = root / subdir;
            REQUIRE(std::filesystem::is_directory(directory));
            std::vector<std::filesystem::path> paths;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json" &&
                    (std::string(subdir) == "model_template" || entry.path().filename() == "config.json")) {
                    paths.push_back(entry.path());
                }
            }
            REQUIRE_FALSE(paths.empty());
            std::sort(paths.begin(), paths.end());
            for (const auto& path : paths) {
                CAPTURE(path);
                std::string source;
                REQUIRE((ModelInfoUtils::LoadJson(path.string(), source) == COSMO_NN_OK));
                PipelineConfig config;
                auto result = pipeline_utils::ParsePipelineConfig(source, config);
                INFO(result.description());
                REQUIRE((result == COSMO_NN_OK));
                REQUIRE_FALSE(config.model_type.empty());
                REQUIRE_FALSE(config.models.empty());
                std::cout << "Parsed " << std::filesystem::relative(path, root).string() << '\n';
            }
        }
    }

#ifdef COSMO_NN_USE_SOPHON_BACKEND
    TEST_CASE("Sophon current pipelines run detection and classification on a real image",
              "[.model-pipeline-device]") {
        const auto* detector_dir   = std::getenv("COSMO_TEST_DETECT_MODEL_DIR");
        const auto* classifier_dir = std::getenv("COSMO_TEST_CLASSIFY_MODEL_DIR");
        const auto* image_path     = std::getenv("COSMO_TEST_IMAGE_PATH");
        REQUIRE(detector_dir != nullptr);
        REQUIRE(classifier_dir != nullptr);
        REQUIRE(image_path != nullptr);
        const std::filesystem::path detection_model(detector_dir);
        const std::filesystem::path classification_model(classifier_dir);
        std::ifstream image_file(image_path, std::ios::binary);
        REQUIRE(image_file.good());
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(image_file)),
                                         std::istreambuf_iterator<char>());
        REQUIRE_FALSE(bytes.empty());

        mem::DeviceContext context;
        test::ScopedServiceOverride<mem::IDeviceContext> registration(context);
        service::MemoryPoolServiceImpl pool;
        media::OsdTextRenderer osd;
        media::VideoFrameProcSophon frames(context.GetMediaHandle(), osd);
        {
            auto decoded = frames.DecodeJpeg(bytes);
            REQUIRE(VideoFrameValid(decoded));
            auto image = frames.I4202BGR(decoded);
            REQUIRE(VideoFrameValid(image));
            for (int iteration = 0; iteration < 3; ++iteration) {
                CAPTURE(iteration);
                AiDetectorUnify detector("fixture-detection", (detection_model / "config.json").string(),
                                         (detection_model / "model.nn").string());
                REQUIRE(detector.Init() == util::ErrorEnum::Success);
                REQUIRE_FALSE(detector.GetLabels().empty());
                std::vector<std::vector<AiDetectRstEl>> detections;
                REQUIRE(detector.Detect({image}, {}, detections) == util::ErrorEnum::Success);
                REQUIRE(detections.size() == 1);
                REQUIRE_FALSE(detections[0].empty());
                for (const auto& detected : detections[0]) {
                    REQUIRE(detected.box.width > 0);
                    REQUIRE(detected.box.height > 0);
                    REQUIRE(std::isfinite(detected.confidence.confidence));
                    std::cout << "Detection " << detected.confidence.label << ' '
                              << detected.confidence.confidence << ' ' << detected.box.x << ' '
                              << detected.box.y << ' ' << detected.box.width << ' ' << detected.box.height
                              << '\n';
                }

                AiClassifierUnify classifier("fixture-classification",
                                             (classification_model / "config.json").string(),
                                             (classification_model / "model.nn").string());
                REQUIRE(classifier.Init() == util::ErrorEnum::Success);
                std::vector<AiDetectRstEl> targets{detections[0].front()};
                REQUIRE(classifier.Classify(image, targets) == util::ErrorEnum::Success);
                REQUIRE_FALSE(targets[0].classifyRst.empty());
                for (const auto& classified : targets[0].classifyRst) {
                    REQUIRE(std::isfinite(classified.confidence));
                    REQUIRE_FALSE(classified.label.empty());
                    std::cout << "Classification " << classified.label << ' ' << classified.confidence
                              << '\n';
                }
            }
        }
        for (const auto& status : pool.Status()) {
            REQUIRE(status.used_cnt == 0);
            REQUIRE(status.used_nodes_status.empty());
        }
    }
#endif

}  // namespace
}  // namespace cosmo::nn
