#include "catch_amalgamated.hpp"

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)

#include <rockchip/mpp_buffer.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>

#include "infer/AiClassifierUnify.h"
#include "infer/AiDetectorUnify.h"
#include "infer/AiRecognizerUnify.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "nn/core/inference_pipeline_metrics.h"

namespace cosmo {
namespace {

    using Json = nlohmann::json;

    class AffineProfiler final : public nn::IProfiler {
    public:
        void ReportNodeTime(const char* name, double milliseconds) override {
            if (std::string_view(name).find("affine_crop") == 0)
                milliseconds_ += milliseconds;
        }
        void ReportGraphInfo(const char*) override {}
        double milliseconds_ = 0;
    };

    class FramePoolScope {
    public:
        explicit FramePoolScope(int bytes) : pool_(std::make_unique<mem::AllocatorCpu>(), {bytes}) {
            mem::SetMemoryPoolContext(&pool_);
        }
        ~FramePoolScope() {
            mem::SetMemoryPoolContext(nullptr);
        }

    private:
        mem::MemoryPoolMng pool_;
    };

    Json TargetOutput(const AiDetectRstEl& target) {
        Json classes = Json::array();
        for (const auto& label : target.classifyRst)
            classes.push_back({label.label, label.confidence});
        return {{"box", {target.box.x, target.box.y, target.box.width, target.box.height}},
                {"label", target.confidence.label},
                {"score", target.confidence.confidence},
                {"classes", std::move(classes)},
                {"feature", target.feature.feature}};
    }

    media::NativeVideoBufferPtr MakeNativeFrame(const std::filesystem::path& path, int width, int height) {
        struct Buffer {
            MppBufferGroup group{nullptr};
            MppBuffer memory{nullptr};
            ~Buffer() {
                if (memory)
                    mpp_buffer_put(memory);
                if (group)
                    mpp_buffer_group_put(group);
            }
        };
        auto buffer        = std::make_shared<Buffer>();
        const size_t bytes = static_cast<size_t>(width) * height * 3 / 2;
        REQUIRE(mpp_buffer_group_get_internal(&buffer->group, MPP_BUFFER_TYPE_DRM) == MPP_OK);
        REQUIRE(mpp_buffer_get(buffer->group, &buffer->memory, bytes) == MPP_OK);
        REQUIRE(mpp_buffer_sync_begin(buffer->memory) == MPP_OK);
        auto* data = static_cast<char*>(mpp_buffer_get_ptr(buffer->memory));
        REQUIRE(data);
        std::ifstream input(path, std::ios::binary);
        const bool read   = bool(input.read(data, bytes));
        const auto result = mpp_buffer_sync_end(buffer->memory);
        REQUIRE(read);
        REQUIRE(result == MPP_OK);

        auto native   = std::make_shared<media::NativeVideoBuffer>();
        native->fd    = mpp_buffer_get_fd(buffer->memory);
        native->bytes = bytes;
        native->width = native->width_stride = width;
        native->height = native->height_stride = height;
        native->format                         = media::NativeVideoBufferFormat::NV12;
        native->color_space                    = media::NativeVideoColorSpace::Bt709;
        native->color_range                    = media::NativeVideoColorRange::Limited;
        native->owner                          = std::move(buffer);
        REQUIRE(native->Valid());
        return native;
    }

    TEST_CASE("RKNN preprocessing executes through the business inference entry points",
              "[.rknn-preprocess-integration]") {
        const char* manifest_path = std::getenv("COSMO_TEST_RKNN_PREPROCESS_MANIFEST");
        REQUIRE(manifest_path);
        const auto path = std::filesystem::absolute(manifest_path);
        const auto root = path.parent_path();
        std::ifstream manifest_stream(path);
        REQUIRE(manifest_stream.good());
        const auto manifest = Json::parse(manifest_stream);
        const auto config   = (root / manifest.at("config").get<std::string>()).string();
        const auto model    = (root / manifest.at("model").get<std::string>()).string();
        const auto kind     = manifest.at("kind").get<std::string>();
        const int width     = manifest.at("width").get<int>();
        const int height    = manifest.at("height").get<int>();
        const int runs      = manifest.value("runs", 30);
        REQUIRE(width > 0);
        REQUIRE(height > 0);
        REQUIRE(runs > 0);
        FramePoolScope pool(width * height * 3);
        auto frame = std::make_shared<media::VideoFrame>(width, height, media::PixelFormat::PIXEL_BGR8);
        REQUIRE(frame->Active());
        std::ifstream pixels(root / manifest.at("bgr").get<std::string>(), std::ios::binary);
        REQUIRE(bool(pixels.read(reinterpret_cast<char*>(frame->GetData()), frame->GetSize())));
        REQUIRE(pixels.peek() == std::ifstream::traits_type::eof());
        media::NativeVideoBufferPtr native;
        if (manifest.contains("nv12")) {
            native = MakeNativeFrame(root / manifest.at("nv12").get<std::string>(), width, height);
            frame  = std::make_shared<media::VideoFrame>(width, height, media::PixelFormat::PIXEL_BGR8, 0, 0,
                                                         [packed = frame] { return packed; });
        }

        AiDetectRstEl prototype;
        const auto box = manifest.value("roi", std::array<int, 4>{0, 0, width, height});
        prototype.box  = util::Box(box[0], box[1], box[2], box[3]);
        AffineProfiler affine_profiler;
        std::function<std::vector<AiDetectRstEl>()> infer;
        if (kind == "detect") {
            auto engine = std::make_shared<AiDetectorUnify>("validation", config, model);
            std::optional<InferenceInputSize> size;
            if (manifest.contains("input_size")) {
                InferenceInputSize requested;
                REQUIRE(ParseInferenceInputSize(manifest.at("input_size").get<std::string>(), requested));
                size = requested;
            }
            REQUIRE(engine->Init(size) == util::ErrorEnum::Success);
            infer = [&, engine] {
                std::vector<std::vector<AiDetectRstEl>> outputs;
                REQUIRE(engine->Detect({frame}, {native}, {}, outputs) == util::ErrorEnum::Success);
                REQUIRE(outputs.size() == 1);
                return std::move(outputs.front());
            };
        } else if (kind == "classify") {
            auto engine = std::make_shared<AiClassifierUnify>("validation", config, model);
            REQUIRE(engine->Init() == util::ErrorEnum::Success);
            infer = [&, engine] {
                std::vector<AiDetectRstEl> outputs{prototype};
                REQUIRE(engine->Classify(frame, outputs, true, native) == util::ErrorEnum::Success);
                REQUIRE_FALSE(outputs.front().classifyRst.empty());
                return outputs;
            };
        } else if (kind == "affine") {
            nn::DefaultComponent::Options options;
            options.profiler = &affine_profiler;
            auto engine = std::make_shared<nn::DefaultComponent>(options, config, model, nn::DEVICE_RKNN);
            std::vector<std::shared_ptr<nn::Blob>> images, landmarks;
            REQUIRE(ConvertImagesToBlobs({frame}, images) == util::ErrorEnum::Success);
            const std::vector<std::vector<int>> points{manifest.at("landmarks").get<std::vector<int>>()};
            REQUIRE(ConvertDatasToBlobsFloat(points, landmarks) == util::ErrorEnum::Success);
            infer = [engine, images, landmarks] {
                REQUIRE(bool(engine->Forward({images, landmarks})));
                std::vector<std::vector<float>> features;
                REQUIRE(bool(engine->ParseOutput<float>(features)));
                REQUIRE(features.size() == 1);
                REQUIRE_FALSE(features.front().empty());
                AiDetectRstEl output;
                output.feature.feature = std::move(features.front());
                return std::vector<AiDetectRstEl>{std::move(output)};
            };
        } else {
            REQUIRE(kind == "feature");
            auto engine = std::make_shared<AiRecognizerUnify>(config, model);
            REQUIRE(engine->Init() == util::ErrorEnum::Success);
            infer = [&, engine] {
                std::vector<AiDetectRstEl> outputs{prototype};
                REQUIRE(engine->Extract(frame, outputs, true, native) == util::ErrorEnum::Success);
                REQUIRE_FALSE(outputs.front().feature.feature.empty());
                return outputs;
            };
        }

        for (int warmup = 0; warmup < 3; ++warmup)
            (void)infer();
        affine_profiler.milliseconds_ = 0;
        const auto before             = nn::GetInferencePipelineMetrics().Snapshot();
        const auto cpu_start          = std::clock();
        const auto wall_start         = std::chrono::steady_clock::now();
        std::vector<AiDetectRstEl> outputs;
        for (int run = 0; run < runs; ++run)
            outputs = infer();
        const double wall_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wall_start).count();
        const double cpu_ms = 1000.0 * (std::clock() - cpu_start) / CLOCKS_PER_SEC;
        const auto after    = nn::GetInferencePipelineMetrics().Snapshot();
        auto report         = Json{{"kind", kind},
                                   {"runs", runs},
                                   {"wall_ms", wall_ms},
                                   {"cpu_ms", cpu_ms},
                                   {"outputs", Json::array()}};
        for (const auto& output : outputs)
            report["outputs"].push_back(TargetOutput(output));
        if (kind == "affine")
            report["affine_crop_ms"] = affine_profiler.milliseconds_;
#define COSMO_RKNN_METRIC(field) report[#field] = after.field - before.field
        COSMO_RKNN_METRIC(rknn_rga_resize_color_calls);
        COSMO_RKNN_METRIC(rknn_rga_resize_color_nanoseconds);
        COSMO_RKNN_METRIC(rknn_rga_fill_nanoseconds);
        COSMO_RKNN_METRIC(rknn_cpu_host_padding_nanoseconds);
        COSMO_RKNN_METRIC(rknn_rga_crop_resize_calls);
        COSMO_RKNN_METRIC(rknn_rga_crop_resize_nanoseconds);
        COSMO_RKNN_METRIC(rknn_mpp_dmabuf_frames);
        COSMO_RKNN_METRIC(rknn_rga_crop_dmabuf_frames);
        COSMO_RKNN_METRIC(rknn_rga_crop_host_fallbacks);
        COSMO_RKNN_METRIC(rknn_cpu_resize_fallback_calls);
        COSMO_RKNN_METRIC(rknn_cpu_resize_fallback_nanoseconds);
        COSMO_RKNN_METRIC(rknn_cpu_crop_resize_fallback_calls);
        COSMO_RKNN_METRIC(rknn_cpu_crop_resize_fallback_nanoseconds);
        COSMO_RKNN_METRIC(rknn_cpu_normalize_fallback_nanoseconds);
        COSMO_RKNN_METRIC(rknn_native_input_map_nanoseconds);
        COSMO_RKNN_METRIC(rknn_rga_bound_native_int8_frames);
        COSMO_RKNN_METRIC(rknn_rga_bound_uint8_frames);
        COSMO_RKNN_METRIC(rknn_rga_bound_requantize_nanoseconds);
        COSMO_RKNN_METRIC(rknn_bound_input_copy_nanoseconds);
        COSMO_RKNN_METRIC(rknn_bound_input_sync_nanoseconds);
        COSMO_RKNN_METRIC(rknn_run_nanoseconds);
        COSMO_RKNN_METRIC(rknn_run_calls);
#undef COSMO_RKNN_METRIC
        REQUIRE(after.rknn_run_calls >= before.rknn_run_calls + static_cast<unsigned>(runs));
        std::ofstream out(root / manifest.at("output").get<std::string>());
        REQUIRE(bool(out << report.dump(2) << '\n'));
    }

}  // namespace
}  // namespace cosmo

#endif
