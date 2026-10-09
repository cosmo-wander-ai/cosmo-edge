#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "catch_amalgamated.hpp"
#include "infer/AiClassifierUnify.h"
#include "infer/AiComponment.h"
#include "infer/AiLandmarkerUnify.h"
#include "infer/AiRecognizerUnify.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "nn/core/abstract_device.h"
#include "nn/device/naive/naive_device.h"
#include "nn/pipeline/model_pipeline.h"

namespace cosmo {
namespace {

    class ControlledDataDevice final : public nn::NaiveDevice {
    public:
        ControlledDataDevice(int fail_on, bool throw_on_failure)
            : NaiveDevice(nn::DEVICE_NAIVE), fail_on_(fail_on), throw_on_failure_(throw_on_failure) {}

        nn::Status Allocate(void** handle, unsigned long* phy, nn::BlobMemorySizeInfo& info) override {
            if (++allocations == fail_on_) {
                *handle = nullptr;
                *phy    = 0;
                if (throw_on_failure_) {
                    throw std::bad_alloc();
                }
                return nn::COSMO_NN_ERR_OUT_OF_MEMORY;
            }
            return NaiveDevice::Allocate(handle, phy, info);
        }

        nn::Status Free(void* handle, unsigned long phy) override {
            ++frees;
            return NaiveDevice::Free(handle, phy);
        }

        int allocations{0};
        int frees{0};

    private:
        int fail_on_;
        bool throw_on_failure_;
    };

    class ScopedDataDevice {
    public:
        explicit ScopedDataDevice(std::shared_ptr<nn::AbstractDevice> replacement)
            : previous_(nn::GetGlobDeviceMap().at(nn::DEVICE_NAIVE)) {
            nn::GetGlobDeviceMap().at(nn::DEVICE_NAIVE) = std::move(replacement);
        }
        ~ScopedDataDevice() {
            nn::GetGlobDeviceMap().at(nn::DEVICE_NAIVE) = std::move(previous_);
        }

    private:
        std::shared_ptr<nn::AbstractDevice> previous_;
    };

    std::shared_ptr<nn::Blob> ExistingOutput() {
        nn::BlobDesc desc;
        desc.data_type = nn::DATA_TYPE_INT32;
        desc.dims      = {1, 1};
        auto blob      = std::make_shared<nn::Blob>(desc, true);
        REQUIRE(blob->GetHandle().base != nullptr);
        *static_cast<int32_t*>(blob->GetHandle().base) = 42;
        return blob;
    }

    TEST_CASE("Data blob conversion rejects ragged matrices without partial output", "[data-blob]") {
        const bool floating = GENERATE(false, true);
        const auto invalid  = GENERATE(std::vector<std::vector<std::vector<int>>>{},
                                       std::vector<std::vector<std::vector<int>>>{{}},
                                       std::vector<std::vector<std::vector<int>>>{{{}}},
                                       std::vector<std::vector<std::vector<int>>>{{{1}, {2, 3}}},
                                       std::vector<std::vector<std::vector<int>>>{{{1, 2}, {3}}},
                                       std::vector<std::vector<std::vector<int>>>{{{}, {1}}},
                                       std::vector<std::vector<std::vector<int>>>{{{1, 2}}, {{3}, {4, 5}}});
        auto sentinel       = ExistingOutput();
        std::vector<std::shared_ptr<nn::Blob>> output{sentinel};
        const auto result =
            floating ? ConvertDatasToBlobsFloat(invalid, output) : ConvertDatasToBlobs(invalid, output);
        CHECK(result == util::ErrorEnum::InvalidParam);
        REQUIRE(output == std::vector<std::shared_ptr<nn::Blob>>{sentinel});
        CHECK(*static_cast<int32_t*>(sentinel->GetHandle().base) == 42);
    }

    TEST_CASE("Data blob conversion appends values and preserves independent shapes", "[data-blob]") {
        const bool floating = GENERATE(false, true);
        const std::vector<std::vector<std::vector<int>>> input{{{1, -2}, {3, 4}}, {{5, 6, 7}}};
        auto sentinel = ExistingOutput();
        std::vector<std::shared_ptr<nn::Blob>> output{sentinel};
        REQUIRE((floating ? ConvertDatasToBlobsFloat(input, output) : ConvertDatasToBlobs(input, output)) ==
                util::ErrorEnum::Success);
        REQUIRE(output.size() == 3);
        CHECK(output[0] == sentinel);
        CHECK(output[1]->GetBlobDesc().dims == nn::DimsVector{2, 2});
        CHECK(output[2]->GetBlobDesc().dims == nn::DimsVector{1, 3});
        CHECK(output[1]->GetBlobDesc().data_type == (floating ? nn::DATA_TYPE_FLOAT : nn::DATA_TYPE_INT32));
        if (floating) {
            const auto* first  = static_cast<float*>(output[1]->GetHandle().base);
            const auto* second = static_cast<float*>(output[2]->GetHandle().base);
            CHECK(std::vector<float>(first, first + 4) == std::vector<float>{1, -2, 3, 4});
            CHECK(std::vector<float>(second, second + 3) == std::vector<float>{5, 6, 7});
        } else {
            const auto* first  = static_cast<int32_t*>(output[1]->GetHandle().base);
            const auto* second = static_cast<int32_t*>(output[2]->GetHandle().base);
            CHECK(std::vector<int32_t>(first, first + 4) == std::vector<int32_t>{1, -2, 3, 4});
            CHECK(std::vector<int32_t>(second, second + 3) == std::vector<int32_t>{5, 6, 7});
        }
    }

    TEST_CASE("Data row conversion preserves values and rejects empty rows", "[data-blob]") {
        const bool floating = GENERATE(false, true);
        const std::vector<std::vector<int>> valid{{1, -2}, {3}};
        std::vector<std::shared_ptr<nn::Blob>> output;
        REQUIRE((floating ? ConvertDatasToBlobsFloat(valid, output) : ConvertDatasToBlobs(valid, output)) ==
                util::ErrorEnum::Success);
        REQUIRE(output.size() == 2);
        CHECK(output[0]->GetBlobDesc().dims == nn::DimsVector{1, 2});
        CHECK(output[1]->GetBlobDesc().dims == nn::DimsVector{1, 1});
        if (floating) {
            CHECK(static_cast<float*>(output[0]->GetHandle().base)[1] == -2.0F);
            CHECK(static_cast<float*>(output[1]->GetHandle().base)[0] == 3.0F);
        } else {
            CHECK(static_cast<int32_t*>(output[0]->GetHandle().base)[1] == -2);
            CHECK(static_cast<int32_t*>(output[1]->GetHandle().base)[0] == 3);
        }
        const auto previous = output;
        for (const auto& invalid : std::vector<std::vector<std::vector<int>>>{{}, {{}}, {{4}, {}}}) {
            CHECK((floating ? ConvertDatasToBlobsFloat(invalid, output)
                            : ConvertDatasToBlobs(invalid, output)) == util::ErrorEnum::InvalidParam);
            CHECK(output == previous);
        }
    }

    TEST_CASE("Data blob allocation failure releases partial allocations", "[data-blob]") {
        const bool floating = GENERATE(false, true);
        const bool matrix   = GENERATE(false, true);
        const bool throws   = GENERATE(false, true);
        const int fail_on   = GENERATE(1, 2);
        auto sentinel       = ExistingOutput();
        std::vector<std::shared_ptr<nn::Blob>> output{sentinel};
        auto device = std::make_shared<ControlledDataDevice>(fail_on, throws);
        ScopedDataDevice override(device);
        util::ErrorEnum result;
        if (matrix) {
            const std::vector<std::vector<std::vector<int>>> input{{{1, 2}}, {{3, 4}}};
            result = floating ? ConvertDatasToBlobsFloat(input, output) : ConvertDatasToBlobs(input, output);
        } else {
            const std::vector<std::vector<int>> input{{1, 2}, {3, 4}};
            result = floating ? ConvertDatasToBlobsFloat(input, output) : ConvertDatasToBlobs(input, output);
        }
        CHECK(result == util::ErrorEnum::NoMem);
        REQUIRE(output == std::vector<std::shared_ptr<nn::Blob>>{sentinel});
        CHECK(*static_cast<int32_t*>(sentinel->GetHandle().base) == 42);
        CHECK(device->allocations == fail_on);
        CHECK(device->frees == fail_on - 1);
    }

    std::atomic<int> forward_calls{0};

    // Loads without a hardware model; a conversion failure must never reach Forward.
    class ConversionPipeline final : public nn::ModelPipeline {
    public:
        nn::Status Init(const nn::PipelineConfig&, const std::string&, nn::DeviceType, int, nn::IProfiler*,
                        const std::string&, const std::string&, bool) override {
            return nn::COSMO_NN_OK;
        }
        nn::Status Forward(std::initializer_list<std::vector<std::shared_ptr<nn::Blob>>>) override {
            ++forward_calls;
            return nn::COSMO_NN_ERR_NET;
        }
        int GetMaxBatchSize() const override {
            return 2;
        }
        std::string GetModelType() const override {
            return "data-blob-conversion-test";
        }
        nn::OutputCategory GetOutputCategory() const override {
            return nn::OutputCategory::RAW;
        }
    };

    REGISTER_MODEL_PIPELINE("data-blob-conversion-test", ConversionPipeline);

    class ConversionFixture {
    public:
        ConversionFixture() : pool_(std::make_unique<mem::AllocatorCpu>(), std::vector<int>{4096}) {
            char pattern[]        = "/tmp/cosmo-data-blob-XXXXXX";
            const auto* directory = mkdtemp(pattern);
            REQUIRE(directory != nullptr);
            directory_ = directory;
            std::ofstream config(ConfigPath());
            config << R"({"model_type":"data-blob-conversion-test","models":[{}]})";
            REQUIRE(config.good());
            mem::SetMemoryPoolContext(&pool_);
            forward_calls = 0;
        }
        ~ConversionFixture() {
            mem::SetMemoryPoolContext(nullptr);
            std::error_code error;
            std::filesystem::remove_all(directory_, error);
        }
        std::string ConfigPath() const {
            return (directory_ / "config.json").string();
        }

    private:
        mem::MemoryPoolMng pool_;
        std::filesystem::path directory_;
    };

    TEST_CASE("Inference wrappers propagate conversion allocation failures",
              "[data-blob][infer-conversion]") {
        const int wrapper  = GENERATE(0, 1, 2);
        const int mode     = GENERATE(0, 1, 2);
        const bool use_box = GENERATE(false, true);
        ConversionFixture fixture;
        auto image = std::make_shared<media::VideoFrame>(16, 16, media::PixelFormat::PIXEL_BGR8);
        REQUIRE(image->Active());
        std::vector<VideoFramePtr> images{image};
        std::vector<AiDetectRstEl> targets(1);
        targets[0].landmark.landmark = {{1, 2}, {3, 4}};
        std::vector<std::vector<AiDetectRstEl>> batches{targets};
        auto device = std::make_shared<ControlledDataDevice>(1, false);
        ScopedDataDevice override(device);
        util::ErrorEnum result;
        if (wrapper == 0) {
            AiClassifierUnify classifier("fixture", fixture.ConfigPath(), "/unused");
            REQUIRE(classifier.Init() == util::ErrorEnum::Success);
            result = mode == 0   ? classifier.Classify(image, targets, use_box)
                     : mode == 1 ? classifier.Classify(images, targets, use_box)
                                 : classifier.Classify(images, batches, use_box);
        } else if (wrapper == 1) {
            AiLandmarkerUnify marker("fixture", fixture.ConfigPath(), "/unused");
            REQUIRE(marker.Init() == util::ErrorEnum::Success);
            result = mode == 0   ? marker.Marker(image, targets)
                     : mode == 1 ? marker.Marker(images, targets)
                                 : marker.Marker(images, batches);
        } else {
            AiRecognizerUnify recognizer("fixture", fixture.ConfigPath(), "/unused");
            REQUIRE(recognizer.Init() == util::ErrorEnum::Success);
            result = mode == 0   ? recognizer.Extract(image, targets, use_box)
                     : mode == 1 ? recognizer.Extract(images, targets, use_box)
                                 : recognizer.Extract(images, batches, use_box);
        }
        CHECK(result == util::ErrorEnum::NoMem);
        CHECK(device->allocations == 1);
        CHECK(forward_calls == 0);
    }

    TEST_CASE("Inference wrappers reject invalid landmarks before Forward", "[data-blob][infer-conversion]") {
        const bool classify = GENERATE(false, true);
        const int mode      = GENERATE(0, 1, 2);
        ConversionFixture fixture;
        auto image = std::make_shared<media::VideoFrame>(16, 16, media::PixelFormat::PIXEL_BGR8);
        REQUIRE(image->Active());
        std::vector<VideoFramePtr> images{image};
        std::vector<AiDetectRstEl> targets(1);
        std::vector<std::vector<AiDetectRstEl>> batches(1, std::vector<AiDetectRstEl>(2));
        batches[0][0].landmark.landmark = {{1, 2}};
        batches[0][1].landmark.landmark = {{3, 4}, {5, 6}};
        util::ErrorEnum result;
        if (classify) {
            AiClassifierUnify classifier("fixture", fixture.ConfigPath(), "/unused");
            REQUIRE(classifier.Init() == util::ErrorEnum::Success);
            result = mode == 0   ? classifier.Classify(image, targets, false)
                     : mode == 1 ? classifier.Classify(images, targets, false)
                                 : classifier.Classify(images, batches, false);
        } else {
            AiRecognizerUnify recognizer("fixture", fixture.ConfigPath(), "/unused");
            REQUIRE(recognizer.Init() == util::ErrorEnum::Success);
            result = mode == 0   ? recognizer.Extract(image, targets, false)
                     : mode == 1 ? recognizer.Extract(images, targets, false)
                                 : recognizer.Extract(images, batches, false);
        }
        CHECK(result == util::ErrorEnum::InvalidParam);
        CHECK(forward_calls == 0);
    }

}  // namespace
}  // namespace cosmo
