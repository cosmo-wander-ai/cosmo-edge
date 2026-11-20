#ifdef COSMO_NN_USE_CPU_BACKEND

#include <algorithm>
#include <array>
#include <filesystem>

#include "catch_amalgamated.hpp"
#include "nlohmann/json.hpp"
#include "nn/pipeline/model_pipeline.h"
#include "onnxruntime_cxx_api.h"
#include "util/UuidUtil.h"

namespace cosmo::nn {
namespace {

    // One normalized detection and seven empty rows, in the layout expected by the selected decoder.
    void WriteDetectionModel(const std::filesystem::path& path, bool xyxy) {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "detection-size-test");
        Ort::Graph graph;
        const Ort::TensorTypeAndShapeInfo image_type(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 3, 320, 640});
        const Ort::TensorTypeAndShapeInfo boxes_type(ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {1, 8, 6});
        std::vector<Ort::ValueInfo> inputs, outputs;
        inputs.emplace_back("images", Ort::TypeInfo::CreateTensorInfo(image_type.GetConst()).GetConst());
        outputs.emplace_back("boxes", Ort::TypeInfo::CreateTensorInfo(boxes_type.GetConst()).GetConst());
        graph.SetInputs(inputs);
        graph.SetOutputs(outputs);
        const std::array<float, 6> box = xyxy ? std::array<float, 6>{0.25f, 0.25f, 0.75f, 0.75f, 1, 1}
                                              : std::array<float, 6>{0.5f, 0.5f, 0.5f, 0.5f, 1, 1};
        const auto shape               = boxes_type.GetShape();
        Ort::AllocatorWithDefaultOptions allocator;
        auto data  = Ort::Value::CreateTensor<float>(allocator, shape.data(), shape.size());
        auto* rows = data.GetTensorMutableData<float>();
        std::fill_n(rows, boxes_type.GetElementCount(), 0.f);
        std::copy(box.begin(), box.end(), rows);
        graph.AddInitializer("boxes", data, false);
        Ort::Model model({{"", 13}});
        model.AddGraph(graph);
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(ORT_DISABLE_ALL);
        options.SetIntraOpNumThreads(1).SetOptimizedModelFilePath(path.c_str());
        Ort::Session session(env, model, options);
    }

    TEST_CASE("Detection pipelines denormalize boxes using the configured image width and height",
              "[nn][detection-postprocess-size]") {
        const auto [type, post] = GENERATE(table<std::string, std::string>({{"yolov5_det", "yolo"},
                                                                            {"yolov8_det", "yolov8"},
                                                                            {"detector", "yolo"},
                                                                            {"detector", "yolov8"},
                                                                            {"detector", "yolo_e2e"}}));
        CAPTURE(type, post);
        PipelineConfig config;
        PipelineModelConfig model;
        model.inputs.push_back({"images", {1, 3, 320, 640}});
        model.outputs.push_back({"boxes", {1, 8, 6}});
        model.params_json = nlohmann::json{{"input_size", {320, 640}}, {"post_type", post}}.dump();
        config.models.push_back(model);
        std::unique_ptr<ModelPipeline> pipeline(ModelPipelineRegistry::Instance().Create(type));
        REQUIRE(pipeline);
        const auto path = std::filesystem::temp_directory_path() / (util::GenerateUUID() + ".onnx");
        WriteDetectionModel(path, post == "yolo_e2e");
        auto status = pipeline->Init(config, path.string(), DEVICE_CPU, 0);
        std::filesystem::remove(path);
        INFO(status.description());
        REQUIRE(bool(status));

        std::vector<uint8_t> pixels(320 * 640 * 3);
        BlobDesc image;
        image.dims         = {1, 320, 640, 3};
        image.data_type    = DATA_TYPE_UINT8;
        image.data_format  = DATA_FORMAT_NHWC;
        image.image_format = IMAGE_BGR;
        auto input         = std::make_shared<Blob>(image, BlobHandle{pixels.data()});
        REQUIRE(bool(pipeline->Forward({{input}})));
        std::vector<std::vector<ObjectInfoV1>> output;
        REQUIRE(bool(pipeline->ParseDetectionOutput(output)));
        REQUIRE(output.size() == 1);
        REQUIRE(output.front().size() == 1);
        const auto& box = output.front().front();
        CHECK(box.x1 == Catch::Approx(160));
        CHECK(box.y1 == Catch::Approx(80));
        CHECK(box.x2 == Catch::Approx(480));
        CHECK(box.y2 == Catch::Approx(240));
    }

}  // namespace
}  // namespace cosmo::nn

#endif
