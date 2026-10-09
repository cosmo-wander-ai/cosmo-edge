#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/core/abstract_device.h"
#include "nn/pipeline/detection_pipeline.h"
#include "nn/utils/default_component.h"

namespace cosmo::nn {
namespace {

    // Opt-in fixture: raw packed image bytes are inputs, never preprocessed tensors.
    // The manifest and public model/data stay outside tracked repository paths.
    class ObbIntegrationProfiler final : public IProfiler {
    public:
        void ReportNodeTime(const char* name, double milliseconds) override {
            node_times.push_back({{"node", name}, {"milliseconds", milliseconds}});
        }
        void ReportGraphInfo(const char* info) override {
            graph_info = info;
        }
        nlohmann::json node_times = nlohmann::json::array();
        std::string graph_info;
    };

    std::filesystem::path ResolveFixturePath(const std::filesystem::path& root, const std::string& name) {
        const std::filesystem::path path(name);
        return path.is_absolute() ? path : root / path;
    }

    TEST_CASE("OBB pipeline rejects unaligned Sophon input before loading a model",
              "[obb][pipeline-config]") {
        PipelineConfig config;
        config.model_type = "yolo26_obb_det";
        PipelineModelConfig model;
        model.inputs.push_back({"images", {1, 3, 640, 672}, DATA_TYPE_FLOAT});
        model.outputs.push_back({"output0", {1, 300, 7}, DATA_TYPE_FLOAT});
        model.params_json = R"({"input_size":[640,672],"gravity":1})";
        config.models.push_back(std::move(model));
        Yolo26ObbPipeline pipeline;
        auto status = pipeline.Init(config, "", DEVICE_SOPHON_TPU, 0, nullptr, "", "", false);
        REQUIRE(static_cast<int>(status) == COSMO_NN_ERR_INVALID_CFG);
        REQUIRE(status.description().find("multiple of 64") != std::string::npos);
    }

    TEST_CASE("OBB pipeline rejects incompatible image storage before preprocessing",
              "[obb][pipeline-config]") {
        BlobDesc desc;
        desc.dims               = {1, 480, 640, 3};
        desc.data_type          = DATA_TYPE_UINT8;
        desc.data_format        = DATA_FORMAT_NHWC;
        desc.image_format       = IMAGE_BGR;
        const int invalid_input = GENERATE(0, 1, 2, 3, 4, 5);
        CAPTURE(invalid_input);
        switch (invalid_input) {
            case 0:
                desc.data_format = DATA_FORMAT_NCHW;
                desc.dims        = {1, 3, 480, 640};
                break;
            case 1:
                desc.data_type = DATA_TYPE_FLOAT;
                break;
            case 2:
                desc.dims[3] = 4;
                break;
            case 3:
                desc.image_format = IMAGE_NV12;
                break;
            case 4:
                desc.dims[0] = 2;
                break;
            case 5:
                desc.dims.pop_back();
                break;
        }
        // No graph or backing buffer: contract rejection precedes preprocessing.
        Yolo26ObbPipeline pipeline;
        auto image  = std::make_shared<Blob>(desc);
        auto status = pipeline.Forward({{image}});
        REQUIRE(static_cast<int>(status) == COSMO_NN_ERR_INVALID_INPUT);
        REQUIRE(status.description().find("NHWC uint8") != std::string::npos);
    }

    TEST_CASE("OBB pipeline runs real original images through configured backend",
              "[.obb-pipeline-integration]") {
        const auto* manifest_name = std::getenv("COSMO_TEST_OBB_MANIFEST");
        REQUIRE(manifest_name != nullptr);
        const auto manifest_path = std::filesystem::absolute(manifest_name);
        std::ifstream manifest_stream(manifest_path);
        REQUIRE(manifest_stream.good());
        const auto manifest    = nlohmann::json::parse(manifest_stream);
        const auto root        = manifest_path.parent_path();
        const auto config_path = ResolveFixturePath(root, manifest.at("config").get<std::string>());
        const auto model_path  = ResolveFixturePath(root, manifest.at("model").get<std::string>());
        const auto output_path = ResolveFixturePath(root, manifest.at("output").get<std::string>());
        const auto backend     = manifest.at("backend").get<std::string>();
        REQUIRE(std::filesystem::is_regular_file(config_path));
        REQUIRE(std::filesystem::is_regular_file(model_path));
        REQUIRE(manifest.at("frames").is_array());
        REQUIRE_FALSE(manifest.at("frames").empty());

        std::ifstream config_stream(config_path);
        const auto config = nlohmann::json::parse(config_stream);
        REQUIRE(config.at("model_type") == "yolo26_obb_det");
        REQUIRE(config.at("models").size() == 1);

        DeviceType selected_device = DEVICE_NAIVE;
        if (backend == "cpu") {
#ifdef COSMO_NN_USE_ONNX_BACKEND
            selected_device = DEVICE_CPU;
#else
            FAIL("This test executable does not contain the ONNX backend");
#endif
        } else if (backend == "sophon") {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
            selected_device = DEVICE_SOPHON_TPU;
#else
            FAIL("This test executable does not contain the Sophon backend");
#endif
        } else {
            FAIL("Manifest backend must be cpu or sophon");
        }

        ObbIntegrationProfiler profiler;
        DefaultComponent::Options options;
        options.profiler = &profiler;
        DefaultComponent pipeline(options, config_path.string(), model_path.string(), selected_device);
        REQUIRE(profiler.graph_info.find("yolo_obb_decode") != std::string::npos);
        REQUIRE(pipeline.GetMaxBatchSize() >= 1);
        nlohmann::json report{{"backend", backend},
                              {"config", config_path.string()},
                              {"model", model_path.string()},
                              {"graph", profiler.graph_info},
                              {"frames", nlohmann::json::array()}};
        size_t total_objects = 0;
        for (const auto& frame : manifest.at("frames")) {
            const auto raw_path = ResolveFixturePath(root, frame.at("path").get<std::string>());
            CAPTURE(raw_path);
            const int width  = frame.at("width").get<int>();
            const int height = frame.at("height").get<int>();
            const auto color = frame.value("color", "BGR");
            REQUIRE(width > 0);
            REQUIRE(height > 0);
            REQUIRE(width <= 8192);
            REQUIRE(height <= 8192);
            REQUIRE((color == "BGR" || color == "RGB"));
            const size_t expected_bytes = static_cast<size_t>(width) * height * 3;
            REQUIRE(std::filesystem::file_size(raw_path) == expected_bytes);
            std::vector<uint8_t> pixels(expected_bytes);
            std::ifstream raw_stream(raw_path, std::ios::binary);
            REQUIRE(raw_stream.read(reinterpret_cast<char*>(pixels.data()), pixels.size()).good());

            BlobDesc desc;
            desc.dims         = {1, height, width, 3};
            desc.data_type    = DATA_TYPE_UINT8;
            desc.data_format  = DATA_FORMAT_NHWC;
            desc.image_format = color == "BGR" ? IMAGE_BGR : IMAGE_RGB;
            BlobHandle host_handle;
            host_handle.base = pixels.data();
            std::shared_ptr<Blob> image;
            if (selected_device == DEVICE_SOPHON_TPU) {
                desc.device_type = DEVICE_SOPHON_TPU;
                image            = std::make_shared<Blob>(desc, true);
                REQUIRE(image->GetHandle().base != nullptr);
                auto* device = GetDevice(DEVICE_SOPHON_TPU);
                REQUIRE(device != nullptr);
                auto target_handle = image->GetHandle();
                auto copied        = device->CopyToDevice(&target_handle, &host_handle, desc, nullptr);
                INFO(copied.description());
                REQUIRE(bool(copied));
            } else {
                desc.device_type = DEVICE_NAIVE;
                image            = std::make_shared<Blob>(desc, host_handle);
            }

            profiler.node_times = nlohmann::json::array();
            auto forwarded      = pipeline.Forward({{image}});
            INFO(forwarded.description());
            REQUIRE(bool(forwarded));
            std::vector<std::vector<ObjectInfoV1>> outputs;
            auto parsed = pipeline.ParseOutput<ObjectInfoV1>(outputs);
            INFO(parsed.description());
            REQUIRE(bool(parsed));
            REQUIRE(outputs.size() == 1);
            REQUIRE(outputs[0].size() >= frame.value("minimum_detections", size_t{0}));
            nlohmann::json objects = nlohmann::json::array();
            for (const auto& object : outputs[0]) {
                REQUIRE(object.oriented_corners.has_value());
                REQUIRE_FALSE(object.infos.empty());
                REQUIRE(std::isfinite(object.infos[0].confidence));
                REQUIRE(object.x2 > object.x1);
                REQUIRE(object.y2 > object.y1);
                const auto bounds = util::QuadBounds(*object.oriented_corners);
                REQUIRE(object.x1 == Catch::Approx(bounds.x1));
                REQUIRE(object.y1 == Catch::Approx(bounds.y1));
                REQUIRE(object.x2 == Catch::Approx(bounds.x2));
                REQUIRE(object.y2 == Catch::Approx(bounds.y2));
                nlohmann::json corners = nlohmann::json::array();
                for (const auto& point : *object.oriented_corners) {
                    REQUIRE(std::isfinite(point.x));
                    REQUIRE(std::isfinite(point.y));
                    corners.push_back({point.x, point.y});
                }
                objects.push_back({{"bbox", {object.x1, object.y1, object.x2, object.y2}},
                                   {"corners", corners},
                                   {"class_id", object.infos[0].class_id},
                                   {"class_name", object.infos[0].class_name},
                                   {"confidence", object.infos[0].confidence}});
            }
            total_objects += objects.size();
            report["frames"].push_back({{"path", raw_path.string()},
                                        {"width", width},
                                        {"height", height},
                                        {"color", color},
                                        {"bytes", expected_bytes},
                                        {"objects", objects},
                                        {"node_times", profiler.node_times}});
        }
        // Empty outputs on every frame cannot establish a real OBB integration result.
        REQUIRE(total_objects > 0);
        report["result"]           = "PASS";
        report["total_detections"] = total_objects;
        std::ofstream output(output_path);
        REQUIRE(output.good());
        output << report.dump(2) << '\n';
        REQUIRE(output.good());
    }

}  // namespace
}  // namespace cosmo::nn
