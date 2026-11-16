#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nlohmann/json.hpp"
#include "nn/core/abstract_device.h"
#include "nn/pipeline/detection_pipeline.h"
#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {
namespace {

    class RawYolo26Pipeline : public Yolo26DetPipeline {
    public:
        Status InitRaw(const PipelineConfig& config, const std::string& path, DeviceType device) {
            const auto& source = config.models.at(0);
            const auto params  = pipeline_utils::ParseJsonObject(source.params_json);
            ModelInfo model;
            model.filename  = source.file_name;
            model.max_batch = source.max_batch;
            for (const auto& def : source.inputs) {
                InputNodeInfo input;
                input.name      = def.name;
                input.shape     = def.shape;
                input.data_type = def.data_type;
                input.ops.push_back(pipeline_utils::MakeResizeOp(
                    pipeline_utils::ReadIntArray(params, "input_size", {640, 640}, 2),
                    pipeline_utils::ReadInt(params, "gravity", 0),
                    pipeline_utils::ReadIntArray(params, "padding_color", {114, 114, 114}, 1)));
                input.ops.push_back(pipeline_utils::MakeNormalizeOp(
                    pipeline_utils::ReadFloatArray(params, "normalize_mean", {0.f, 0.f, 0.f}, 3),
                    pipeline_utils::ReadFloat(params, "normalize_scale", 0.00392157f),
                    pipeline_utils::ReadBool(params, "is_bgr", true)));
                model.input_node_infos.push_back(std::move(input));
            }
            for (const auto& def : source.outputs) {
                OutputNodeInfo output;
                output.name      = def.name;
                output.shape     = def.shape;
                output.data_type = def.data_type;
                model.output_node_infos.push_back(std::move(output));
            }
            model_info_.models.push_back(std::move(model));
            return InitGraph(path, device, 0, nullptr, "");
        }

        std::vector<std::shared_ptr<Blob>> RawOutput() {
            return GetGraphOutput();
        }
    };

    TEST_CASE("YOLO26 pipeline rejects nonboolean raw overrides before loading a model",
              "[nn][yolo26][pipeline]") {
        for (const auto& value : std::vector<nlohmann::json>{0, 1, "true", nullptr}) {
            PipelineConfig config;
            config.model_type = "yolo26_det";
            PipelineModelConfig model;
            model.params_json = nlohmann::json{{"raw_output", value}}.dump();
            config.models.push_back(model);
            Yolo26DetPipeline pipeline;
            auto status = pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "");
            CHECK(static_cast<int>(status) == COSMO_NN_ERR_INVALID_CFG);
        }
    }

    TEST_CASE("YOLO26 pipeline rejects missing or multiple outputs", "[nn][yolo26][pipeline]") {
        for (int count : {0, 2}) {
            PipelineConfig config;
            config.model_type = "yolo26_det";
            PipelineModelConfig model;
            model.outputs.resize(count);
            config.models.push_back(model);
            Yolo26DetPipeline pipeline;
            auto status = pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "");
            CHECK(static_cast<int>(status) == COSMO_NN_ERR_INVALID_CFG);
        }
    }

    // Models and packed original images are external, opt-in fixtures, as in the OBB integration test.
    // Dump network output separately so an independent reference can verify NMS and coordinate scaling.
    TEST_CASE("YOLO26 pipeline runs original images without raw params", "[.yolo26-pipeline-integration]") {
        const char* manifest_name = std::getenv("COSMO_TEST_YOLO26_MANIFEST");
        REQUIRE(manifest_name != nullptr);
        const auto manifest_path = std::filesystem::absolute(manifest_name);
        std::ifstream manifest_stream(manifest_path);
        REQUIRE(manifest_stream.good());
        const auto manifest    = nlohmann::json::parse(manifest_stream);
        const auto root        = manifest_path.parent_path();
        const auto config_path = root / manifest.at("config").get<std::string>();
        const auto model_path  = root / manifest.at("model").get<std::string>();
        const auto output_path = root / manifest.at("output").get<std::string>();
        std::ifstream config_stream(config_path);
        REQUIRE(config_stream.good());
        const auto json_config = nlohmann::json::parse(config_stream);
        REQUIRE(json_config.at("model_type") == "yolo26_det");
        REQUIRE(json_config.at("models").size() == 1);
        REQUIRE_FALSE(json_config.at("models")[0].at("params").contains("raw_output"));
        PipelineConfig config;
        REQUIRE(bool(pipeline_utils::ParsePipelineConfig(json_config.dump(), config)));
        DeviceType device  = DEVICE_NAIVE;
        const auto backend = manifest.at("backend").get<std::string>();
        if (backend == "cpu") {
#ifdef COSMO_NN_USE_ONNX_BACKEND
            device = DEVICE_CPU;
#else
            FAIL("This executable does not contain the ONNX backend");
#endif
        } else if (backend == "sophon") {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
            device = DEVICE_SOPHON_TPU;
#else
            FAIL("This executable does not contain the Sophon backend");
#endif
        } else {
            FAIL("Fixture backend must be cpu or sophon");
        }
        Yolo26DetPipeline pipeline;
        auto status = pipeline.Init(config, model_path.string(), device, 0, nullptr, "", "");
        INFO(status.description());
        REQUIRE(bool(status));
        RawYolo26Pipeline raw_pipeline;
        status = raw_pipeline.InitRaw(config, model_path.string(), device);
        INFO(status.description());
        REQUIRE(bool(status));
        REQUIRE_FALSE(manifest.at("frames").empty());
        nlohmann::json report{{"backend", backend}, {"frames", nlohmann::json::array()}};
        size_t total = 0;
        for (const auto& frame : manifest.at("frames")) {
            const int width  = frame.at("width").get<int>();
            const int height = frame.at("height").get<int>();
            REQUIRE(width > 0);
            REQUIRE(height > 0);
            REQUIRE(width <= 8192);
            REQUIRE(height <= 8192);
            const auto path    = root / frame.at("path").get<std::string>();
            const size_t bytes = static_cast<size_t>(width) * height * 3;
            REQUIRE(std::filesystem::file_size(path) == bytes);
            std::vector<uint8_t> pixels(bytes);
            std::ifstream input(path, std::ios::binary);
            REQUIRE(input.read(reinterpret_cast<char*>(pixels.data()), bytes).good());
            BlobDesc desc;
            desc.dims         = {1, height, width, 3};
            desc.data_type    = DATA_TYPE_UINT8;
            desc.data_format  = DATA_FORMAT_NHWC;
            desc.image_format = IMAGE_BGR;
            BlobHandle handle;
            handle.base = pixels.data();
            std::shared_ptr<Blob> image;
            if (device == DEVICE_SOPHON_TPU) {
                desc.device_type = device;
                image            = std::make_shared<Blob>(desc, true);
                auto* target     = GetDevice(device);
                REQUIRE(target != nullptr);
                auto destination = image->GetHandle();
                REQUIRE(bool(target->CopyToDevice(&destination, &handle, desc, nullptr)));
            } else {
                image = std::make_shared<Blob>(desc, handle);
            }
            status = pipeline.Forward({{image}});
            INFO(status.description());
            REQUIRE(bool(status));
            std::vector<std::vector<ObjectInfoV1>> outputs;
            REQUIRE(bool(pipeline.ParseDetectionOutput(outputs)));
            REQUIRE(outputs.size() == 1);
            nlohmann::json objects = nlohmann::json::array();
            for (const auto& object : outputs[0]) {
                REQUIRE_FALSE(object.infos.empty());
                REQUIRE(std::isfinite(object.infos[0].confidence));
                REQUIRE(object.x2 > object.x1);
                REQUIRE(object.y2 > object.y1);
                objects.push_back({{"bbox", {object.x1, object.y1, object.x2, object.y2}},
                                   {"class_id", object.infos[0].class_id},
                                   {"confidence", object.infos[0].confidence}});
            }
            REQUIRE(bool(raw_pipeline.Forward({{image}})));
            const auto raw = raw_pipeline.RawOutput();
            REQUIRE(raw.size() == 1);
            const auto& raw_desc = raw[0]->GetBlobDesc();
            REQUIRE(raw_desc.data_type == DATA_TYPE_FLOAT);
            REQUIRE(UsesHostMemory(raw_desc.device_type));
            size_t count = 1;
            for (int dim : raw_desc.dims) {
                REQUIRE(dim > 0);
                count *= dim;
            }
            const auto raw_path =
                output_path.string() + "." + std::to_string(report["frames"].size()) + ".bin";
            std::ofstream raw_stream(raw_path, std::ios::binary);
            raw_stream.write(static_cast<const char*>(raw[0]->GetHandle().base), count * sizeof(float));
            REQUIRE(raw_stream.good());
            report["frames"].push_back({{"width", width},
                                        {"height", height},
                                        {"objects", objects},
                                        {"raw_shape", raw_desc.dims},
                                        {"raw_path", raw_path}});
            total += objects.size();
        }
        REQUIRE(total > 0);
        report["total_detections"] = total;
        std::ofstream output(output_path);
        output << report.dump(2) << '\n';
        REQUIRE(output.good());
    }

}  // namespace
}  // namespace cosmo::nn
