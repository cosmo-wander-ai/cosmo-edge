#include "catch_amalgamated.hpp"

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)

#include <rockchip/mpp_buffer.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include "nn/core/inference_pipeline_metrics.h"
#include "nn/core/shared_resource.h"
#include "nn/device/rknn/rknn_net_node.h"
#include "nn/device/rknn/rknn_preprocess_node.h"
#include "nn/utils/op.h"

namespace {

class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const char* value) : name_(name) {
        if (const char* current = std::getenv(name))
            previous_ = current;
        setenv(name, value, 1);
    }
    ~ScopedEnvironment() {
        if (previous_)
            setenv(name_.c_str(), previous_->c_str(), 1);
        else
            unsetenv(name_.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

class StubBoundInputProvider final : public cosmo::nn::RknnBoundInputProvider {
public:
    bool EnsureRgaBoundInput(int /*height*/, int /*width*/, std::string& reason) override {
        reason.clear();
        return true;
    }
};

cosmo::nn::BlobDesc PackedImageDesc(int height, int width, cosmo::nn::ImageFormat format,
                                    cosmo::nn::DataType type = cosmo::nn::DATA_TYPE_UINT8) {
    cosmo::nn::BlobDesc desc;
    desc.device_type  = cosmo::nn::DEVICE_NAIVE;
    desc.data_type    = type;
    desc.data_format  = cosmo::nn::DATA_FORMAT_NHWC;
    desc.image_format = format;
    desc.dims         = {1, height, width, 3};
    return desc;
}

}  // namespace

TEST_CASE("RKNN detector fast preprocessing contracts are exact", "[nn][rknn][fast-preprocess]") {
    using namespace cosmo::nn;
    CHECK(IsRknnDetectorResizeContract(640, 640, 1, {114, 114, 114}));
    CHECK_FALSE(IsRknnDetectorResizeContract(640, 640, 0, {114, 114, 114}));
    CHECK_FALSE(IsRknnDetectorResizeContract(224, 224, 1, {114, 114, 114}));
    CHECK(IsRknnNativeNormalizeContract({0.0f, 0.0f, 0.0f}, {}, 0.00392157f, {1, 640, 640, 3}));
    CHECK_FALSE(IsRknnNativeNormalizeContract({1.0f, 0.0f, 0.0f}, {}, 0.00392157f, {1, 640, 640, 3}));
    CHECK(IsRknnNativeNormalizeContract({0.0f, 0.0f, 0.0f}, {}, 0.00392157f, {1, 224, 224, 3}));

    const std::array<uint8_t, 6> rgb{0, 127, 255, 255, 1, 128};
    std::array<int8_t, 6> native{};
    MapPackedU8ToNativeInt8(rgb.data(), native.data(), 2, false);
    CHECK((native == std::array<int8_t, 6>{-128, -1, 127, 127, -127, 0}));
    MapPackedU8ToNativeInt8(rgb.data(), native.data(), 2, true);
    CHECK((native == std::array<int8_t, 6>{127, -1, -128, 0, -127, 127}));
}

TEST_CASE("RKNN native input contract requires the model quantization identity",
          "[nn][rknn][fast-preprocess]") {
    using namespace cosmo::nn;
    rknn_tensor_attr attr{};
    attr.n_dims     = 4;
    attr.dims[0]    = 1;
    attr.dims[1]    = 640;
    attr.dims[2]    = 640;
    attr.dims[3]    = 3;
    attr.fmt        = RKNN_TENSOR_NHWC;
    attr.type       = RKNN_TENSOR_INT8;
    attr.qnt_type   = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
    attr.zp         = -128;
    attr.scale      = 0.00392157f;
    const auto desc = PackedImageDesc(640, 640, IMAGE_RGB, DATA_TYPE_INT8);
    CHECK(IsRknnNativeInt8InputCompatible(attr, desc));
    attr.zp = 0;
    CHECK_FALSE(IsRknnNativeInt8InputCompatible(attr, desc));
}

TEST_CASE("RKNN bound input validates native stride and copies packed rows", "[nn][rknn][bound-input]") {
    using namespace cosmo::nn;
    rknn_tensor_attr attr{};
    attr.n_dims           = 4;
    attr.dims[0]          = 1;
    attr.dims[1]          = 2;
    attr.dims[2]          = 2;
    attr.dims[3]          = 3;
    attr.fmt              = RKNN_TENSOR_NHWC;
    attr.type             = RKNN_TENSOR_INT8;
    attr.qnt_type         = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
    attr.zp               = -128;
    attr.scale            = 0.00392157f;
    attr.w_stride         = 4;
    attr.size             = 12;
    attr.size_with_stride = 24;
    const auto desc       = PackedImageDesc(2, 2, IMAGE_RGB, DATA_TYPE_INT8);
    std::string reason;
    CHECK(IsRknnBoundInt8InputCompatible(attr, desc, &reason));
    CHECK(reason.empty());

    const std::array<int8_t, 12> source{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    std::array<int8_t, 24> destination{};
    destination.fill(-1);
    REQUIRE(CopyRknnPackedInt8Input(source.data(), source.size(), destination.data(), destination.size(), 2,
                                    2, 3, 4, &reason));
    CHECK(std::equal(source.begin(), source.begin() + 6, destination.begin()));
    CHECK(std::equal(source.begin() + 6, source.end(), destination.begin() + 12));
    CHECK(std::all_of(destination.begin() + 6, destination.begin() + 12,
                      [](int8_t value) { return value == -1; }));

    std::array<int8_t, 12> compact{};
    REQUIRE(CopyRknnPackedInt8Input(source.data(), source.size(), compact.data(), compact.size(), 2, 2, 3, 0,
                                    &reason));
    CHECK(compact == source);

    attr.w_stride = 1;
    CHECK_FALSE(IsRknnBoundInt8InputCompatible(attr, desc, &reason));
    CHECK(reason.find("stride") != std::string::npos);
    CHECK_FALSE(
        CopyRknnPackedInt8Input(source.data(), source.size(), destination.data(), 8, 2, 2, 3, 2, &reason));
    CHECK(reason.find("smaller") != std::string::npos);
}

TEST_CASE("RKNN RGA bound input validates the native tensor and DMA-BUF target stride",
          "[nn][rknn][bound-input][rga]") {
    using namespace cosmo::nn;
    CHECK(IsRknnRgbUint8InputContract(kRknnRgbUint8InputContract));
    CHECK_FALSE(IsRknnRgbUint8InputContract("host-normalized-int8"));

    rknn_tensor_attr attr{};
    attr.n_dims           = 4;
    attr.dims[0]          = 1;
    attr.dims[1]          = 2;
    attr.dims[2]          = 2;
    attr.dims[3]          = 3;
    attr.fmt              = RKNN_TENSOR_NHWC;
    attr.type             = RKNN_TENSOR_INT8;
    attr.qnt_type         = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
    attr.zp               = -128;
    attr.scale            = 0.00392157f;
    attr.w_stride         = 4;
    attr.size             = 12;
    attr.size_with_stride = 24;
    std::string reason;
    CHECK(IsRknnRgaBoundInputCompatible(attr, 2, 2, &reason));

    auto incompatible = attr;
    incompatible.zp   = 0;
    CHECK_FALSE(IsRknnRgaBoundInputCompatible(incompatible, 2, 2, &reason));
    CHECK(reason.find("quantization") != std::string::npos);

    StubBoundInputProvider provider;
    uint8_t storage[24]{};
    RknnBoundInputTarget target;
    target.owner           = &provider;
    target.virtual_address = storage;
    target.fd              = 3;
    target.bytes           = sizeof(storage);
    target.height          = 2;
    target.width           = 2;
    target.channels        = 3;
    target.width_stride    = 4;
    CHECK(target.Matches(2, 2));
    target.bytes = 23;
    CHECK_FALSE(target.Matches(2, 2));
}

TEST_CASE("RKNN RGA bound input requantizes UINT8 pixels in place without touching stride padding",
          "[nn][rknn][bound-input][rga]") {
    using namespace cosmo::nn;
    std::array<uint8_t, 24> pixels{};
    pixels.fill(99);
    const std::array<uint8_t, 12> packed{0, 1, 127, 128, 254, 255, 255, 128, 127, 126, 1, 0};
    std::copy(packed.begin(), packed.begin() + 6, pixels.begin());
    std::copy(packed.begin() + 6, packed.end(), pixels.begin() + 12);
    std::string reason;
    REQUIRE(RequantizeRknnPackedUint8ToInt8InPlace(pixels.data(), pixels.size(), 2, 2, 3, 4, &reason));
    CHECK((std::array<uint8_t, 6>{pixels[0], pixels[1], pixels[2], pixels[3], pixels[4], pixels[5]}) ==
          std::array<uint8_t, 6>{128, 129, 255, 0, 126, 127});
    CHECK((std::array<uint8_t, 6>{pixels[12], pixels[13], pixels[14], pixels[15], pixels[16], pixels[17]}) ==
          std::array<uint8_t, 6>{127, 0, 255, 254, 129, 128});
    CHECK(std::all_of(pixels.begin() + 6, pixels.begin() + 12, [](uint8_t value) { return value == 99; }));
    CHECK(std::all_of(pixels.begin() + 18, pixels.end(), [](uint8_t value) { return value == 99; }));
    CHECK(reason.empty());

    std::array<uint8_t, 128> vector_sized{};
    std::iota(vector_sized.begin(), vector_sized.end(), uint8_t{0});
    auto expected = vector_sized;
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](uint8_t value) { return static_cast<uint8_t>(value ^ 0x80); });
    REQUIRE(RequantizeRknnPackedUint8ToInt8InPlace(vector_sized.data(), vector_sized.size(), 1, 128, 1, 128,
                                                   &reason));
    CHECK(vector_sized == expected);
    CHECK(std::string(RknnRgaBoundRequantizeImplementation()).find("xor-sign-bit") != std::string::npos);

    CHECK_FALSE(RequantizeRknnPackedUint8ToInt8InPlace(pixels.data(), 8, 2, 2, 3, 2, &reason));
    CHECK(reason.find("smaller") != std::string::npos);
}

TEST_CASE("RKNN UINT8 model contract restores raw pixels for FP16 runtime input",
          "[nn][rknn][input-contract]") {
    using namespace cosmo::nn;
    const std::array<int8_t, 12> native{-128, -127, -1, 0, 126, 127, 127, 0, -1, -2, -127, -128};
    std::array<uint8_t, 24> restored{};
    restored.fill(99);
    std::string reason;
    REQUIRE(CopyRknnPackedNativeInt8ToUint8(native.data(), native.size(), restored.data(), restored.size(), 2,
                                            2, 3, 4, &reason));
    CHECK((std::array<uint8_t, 6>{restored[0], restored[1], restored[2], restored[3], restored[4],
                                  restored[5]}) == std::array<uint8_t, 6>{0, 1, 127, 128, 254, 255});
    CHECK((std::array<uint8_t, 6>{restored[12], restored[13], restored[14], restored[15], restored[16],
                                  restored[17]}) == std::array<uint8_t, 6>{255, 128, 127, 126, 1, 0});
    CHECK(
        std::all_of(restored.begin() + 6, restored.begin() + 12, [](uint8_t value) { return value == 99; }));
    CHECK(std::all_of(restored.begin() + 18, restored.end(), [](uint8_t value) { return value == 99; }));
    CHECK(reason.empty());

    const std::array<float, 6> normalized{0.0f, 1.0f / 255.0f, 0.5f, 1.0f, -0.1f, 1.1f};
    std::array<uint8_t, 6> pixels{};
    REQUIRE(ConvertRknnNormalizedFloatToUint8(normalized.data(), normalized.size(), pixels.data(),
                                              pixels.size(), &reason));
    CHECK((pixels == std::array<uint8_t, 6>{0, 1, 128, 255, 0, 255}));
    CHECK(reason.empty());
}

TEST_CASE("RKNN bound input switch defaults on and supports explicit rollback", "[nn][rknn][bound-input]") {
    using namespace cosmo::nn;
    {
        ScopedEnvironment enabled("COSMO_RKNN_BOUND_INPUT", "1");
        CHECK(RknnBoundInputEnabled());
    }
    {
        ScopedEnvironment disabled("COSMO_RKNN_BOUND_INPUT", "0");
        CHECK_FALSE(RknnBoundInputEnabled());
    }
}

TEST_CASE("RKNN core scheduling maps explicit and split modes deterministically",
          "[nn][rknn][core-scheduling]") {
    using namespace cosmo::nn;
    bool valid = false;
    CHECK(ParseRknnCoreMode(" auto ", &valid) == RknnCoreMode::Auto);
    CHECK(valid);
    CHECK(ParseRknnCoreMode("CORE0", &valid) == RknnCoreMode::Core0);
    CHECK(valid);
    CHECK(ParseRknnCoreMode("core_1", &valid) == RknnCoreMode::Core1);
    CHECK(valid);
    CHECK(ParseRknnCoreMode("dual", &valid) == RknnCoreMode::Core01);
    CHECK(valid);
    CHECK(ParseRknnCoreMode("split", &valid) == RknnCoreMode::Split);
    CHECK(valid);
    CHECK(ParseRknnCoreMode("unsupported", &valid) == RknnCoreMode::Auto);
    CHECK_FALSE(valid);

    CHECK(ResolveRknnCoreMask(RknnCoreMode::Auto, 7) == RKNN_NPU_CORE_AUTO);
    CHECK(ResolveRknnCoreMask(RknnCoreMode::Core0, 7) == RKNN_NPU_CORE_0);
    CHECK(ResolveRknnCoreMask(RknnCoreMode::Core1, 7) == RKNN_NPU_CORE_1);
    CHECK(ResolveRknnCoreMask(RknnCoreMode::Core01, 7) == RKNN_NPU_CORE_0_1);
    CHECK(ResolveRknnCoreMask(RknnCoreMode::Split, 0) == RKNN_NPU_CORE_0);
    CHECK(ResolveRknnCoreMask(RknnCoreMode::Split, 1) == RKNN_NPU_CORE_1);
    CHECK(ResolveRknnCoreMask(RknnCoreMode::Split, 2) == RKNN_NPU_CORE_0);
    CHECK_FALSE(ShouldConfigureRknnCoreMask(RknnCoreMode::Auto));
    CHECK(ShouldConfigureRknnCoreMask(RknnCoreMode::Core0));
    CHECK(ShouldConfigureRknnCoreMask(RknnCoreMode::Core1));
    CHECK(ShouldConfigureRknnCoreMask(RknnCoreMode::Core01));
    CHECK(ShouldConfigureRknnCoreMask(RknnCoreMode::Split));
    CHECK(std::string(RknnCoreModeName(RknnCoreMode::Core01)) == "core0_1");
}

TEST_CASE("RKNN MPP DMA-BUF switch defaults on and supports explicit rollback", "[nn][rknn][mpp-dmabuf]") {
    using namespace cosmo::nn;
    {
        ScopedEnvironment enabled("COSMO_RKNN_MPP_DMABUF", "1");
        CHECK(RknnMppDmaBufEnabled());
    }
    {
        ScopedEnvironment disabled("COSMO_RKNN_MPP_DMABUF", "0");
        CHECK_FALSE(RknnMppDmaBufEnabled());
    }
    {
        ScopedEnvironment forced("COSMO_RKNN_MPP_DMABUF_FORCE_FAIL", "1");
        CHECK(RknnForceMppDmaBufFailure());
    }

    BlobHandle handle;
    handle.native_image.fd            = 7;
    handle.native_image.bytes         = 1920 * 1088 * 3 / 2;
    handle.native_image.width         = 1920;
    handle.native_image.height        = 1080;
    handle.native_image.width_stride  = 1920;
    handle.native_image.height_stride = 1088;
    handle.native_image.format        = IMAGE_NV12;
    CHECK(handle.native_image.Valid());
    handle.native_image.width_stride = 1919;
    CHECK_FALSE(handle.native_image.Valid());
}

TEST_CASE("RKNN native output capability excludes FP16 and malformed YOLOv8 heads",
          "[nn][rknn][fast-output][fp16]") {
    using namespace cosmo::nn;
    const std::array<std::array<uint32_t, 4>, 6> shapes{{
        {{1, 64, 80, 80}},
        {{1, 80, 80, 80}},
        {{1, 64, 40, 40}},
        {{1, 80, 40, 40}},
        {{1, 64, 20, 20}},
        {{1, 80, 20, 20}},
    }};
    std::vector<rknn_tensor_attr> attrs(shapes.size());
    for (size_t index = 0; index < attrs.size(); ++index) {
        auto& attr    = attrs[index];
        attr.index    = static_cast<uint32_t>(index);
        attr.n_dims   = 4;
        attr.fmt      = RKNN_TENSOR_NCHW;
        attr.type     = RKNN_TENSOR_INT8;
        attr.qnt_type = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
        attr.zp       = index % 2 == 0 ? -61 : 114;
        attr.scale    = index % 2 == 0 ? 0.11488f : 0.113557f;
        size_t count  = 1;
        for (size_t dim = 0; dim < shapes[index].size(); ++dim) {
            attr.dims[dim] = shapes[index][dim];
            count *= shapes[index][dim];
        }
        attr.n_elems = static_cast<uint32_t>(count);
        attr.size    = static_cast<uint32_t>(count);
    }

    std::string reason;
    CHECK(IsRknnNativeYolov8OutputCompatible(attrs, &reason));
    CHECK(reason.empty());

    auto fp16    = attrs;
    fp16[0].type = RKNN_TENSOR_FLOAT16;
    fp16[0].size *= 2;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(fp16, &reason));
    CHECK(reason.find("FP16") != std::string::npos);

    auto wrong_format   = attrs;
    wrong_format[0].fmt = RKNN_TENSOR_NHWC;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(wrong_format));
    auto wrong_size = attrs;
    wrong_size[0].size += 1;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(wrong_size));
    auto wrong_quantization     = attrs;
    wrong_quantization[0].scale = 0.0f;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(wrong_quantization));

    const std::array<std::array<uint32_t, 4>, 9> score_sum_shapes{{
        {{1, 64, 80, 80}},
        {{1, 80, 80, 80}},
        {{1, 1, 80, 80}},
        {{1, 64, 40, 40}},
        {{1, 80, 40, 40}},
        {{1, 1, 40, 40}},
        {{1, 64, 20, 20}},
        {{1, 80, 20, 20}},
        {{1, 1, 20, 20}},
    }};
    std::vector<rknn_tensor_attr> score_sum_attrs(score_sum_shapes.size());
    for (size_t index = 0; index < score_sum_attrs.size(); ++index) {
        auto& attr    = score_sum_attrs[index];
        attr.index    = static_cast<uint32_t>(index);
        attr.n_dims   = 4;
        attr.fmt      = RKNN_TENSOR_NCHW;
        attr.type     = RKNN_TENSOR_INT8;
        attr.qnt_type = RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC;
        attr.zp       = 0;
        attr.scale    = index % 3 == 0 ? 0.1f : 0.01f;
        size_t count  = 1;
        for (size_t dim = 0; dim < score_sum_shapes[index].size(); ++dim) {
            attr.dims[dim] = score_sum_shapes[index][dim];
            count *= score_sum_shapes[index][dim];
        }
        attr.n_elems = static_cast<uint32_t>(count);
        attr.size    = static_cast<uint32_t>(count);
    }
    CHECK(IsRknnNativeYolov8OutputCompatible(score_sum_attrs, &reason));
    auto score_sum_fp16    = score_sum_attrs;
    score_sum_fp16[4].type = RKNN_TENSOR_FLOAT16;
    score_sum_fp16[4].size *= 2;
    CHECK_FALSE(IsRknnNativeYolov8OutputCompatible(score_sum_fp16, &reason));
    CHECK(reason.find("FP16") != std::string::npos);
}

TEST_CASE("RKNN native output switch defaults on and supports explicit rollback", "[nn][rknn][fast-output]") {
    using namespace cosmo::nn;
    {
        ScopedEnvironment enabled("COSMO_RKNN_FAST_OUTPUT", "1");
        CHECK(RknnFastOutputEnabled());
    }
    {
        ScopedEnvironment disabled("COSMO_RKNN_FAST_OUTPUT", "0");
        CHECK_FALSE(RknnFastOutputEnabled());
    }
    {
        ScopedEnvironment enabled("COSMO_RKNN_DIRECT_CANDIDATES", "1");
        CHECK(RknnDirectCandidatesEnabled());
    }
    {
        ScopedEnvironment disabled("COSMO_RKNN_DIRECT_CANDIDATES", "0");
        CHECK_FALSE(RknnDirectCandidatesEnabled());
    }
}

TEST_CASE("RKNN classifier-sized normalization uses the shared native INT8 layout",
          "[nn][rknn][fast-preprocess]") {
    using namespace cosmo::nn;
    Normalize normalize;
    normalize.mean   = {0.0f, 0.0f, 0.0f};
    normalize.scale  = 0.00392157f;
    normalize.is_bgr = false;
    SharedResource resource;
    RknnNormalizeNode node;
    node.SetSharedResource(&resource);
    node.LoadParam(&normalize);
    REQUIRE(bool(node.InferTopShapesWithBottoms({{1, 224, 224, 3}}, {DATA_TYPE_UINT8})));
    CHECK(node.GetTopBlobDataTypes().front() == DATA_TYPE_INT8);
    CHECK((node.GetTopBlobShapes().front() == DimsVector{1, 224, 224, 3}));
    CHECK(resource.rknn_bound_input_preprocess_compatible);

    auto bottom_desc = PackedImageDesc(224, 224, IMAGE_BGR);
    auto bottom      = std::make_shared<Blob>(bottom_desc, true);
    BlobDesc top_desc;
    top_desc.device_type = DEVICE_NAIVE;
    top_desc.data_type   = DATA_TYPE_INT8;
    top_desc.data_format = DATA_FORMAT_NHWC;
    top_desc.dims        = {1, 224, 224, 3};
    auto top             = std::make_shared<Blob>(top_desc, true);
    auto* input          = static_cast<uint8_t*>(bottom->GetHandle().base);
    for (size_t pixel = 0; pixel < static_cast<size_t>(224) * 224; ++pixel) {
        input[pixel * 3]     = 10;
        input[pixel * 3 + 1] = 20;
        input[pixel * 3 + 2] = 30;
    }
    std::vector<std::shared_ptr<Blob>> bottoms{bottom};
    std::vector<std::shared_ptr<Blob>> tops{top};
    REQUIRE(bool(node.Forward(bottoms, tops)));
    const auto* output = static_cast<const int8_t*>(top->GetHandle().base);
    CHECK(output[0] == -98);
    CHECK(output[1] == -108);
    CHECK(output[2] == -118);
}

TEST_CASE("RKNN normalize bypasses host mapping for an RGA-bound frame", "[nn][rknn][bound-input][rga]") {
    using namespace cosmo::nn;
    Normalize normalize;
    normalize.mean   = {0.0f, 0.0f, 0.0f};
    normalize.scale  = 0.00392157f;
    normalize.is_bgr = false;
    SharedResource resource;
    StubBoundInputProvider provider;
    resource.rknn_bound_input_provider = &provider;
    RknnNormalizeNode node;
    node.SetSharedResource(&resource);
    node.LoadParam(&normalize);
    REQUIRE(bool(node.InferTopShapesWithBottoms({{1, 640, 640, 3}}, {DATA_TYPE_UINT8})));

    auto bottom = std::make_shared<Blob>(PackedImageDesc(640, 640, IMAGE_RGB), true);
    BlobDesc top_desc;
    top_desc.device_type = DEVICE_NAIVE;
    top_desc.data_type   = node.GetTopBlobDataTypes().front();
    top_desc.data_format = DATA_FORMAT_NHWC;
    top_desc.dims        = node.GetTopBlobShapes().front();
    auto top             = std::make_shared<Blob>(top_desc, true);
    auto* output         = static_cast<int8_t*>(top->GetHandle().base);
    const size_t bytes   = static_cast<size_t>(640) * 640 * 3;
    std::fill(output, output + bytes, static_cast<int8_t>(42));

    auto& target           = resource.rknn_bound_input_target;
    target.owner           = &provider;
    target.virtual_address = bottom->GetHandle().base;
    target.fd              = 3;
    target.bytes           = bytes;
    target.height          = 640;
    target.width           = 640;
    target.channels        = 3;
    target.width_stride    = 640;
    target.generation      = 1;
    target.frame_ready     = true;

    const auto before = GetInferencePipelineMetrics().Snapshot();
    std::vector<std::shared_ptr<Blob>> bottoms{bottom};
    std::vector<std::shared_ptr<Blob>> tops{top};
    REQUIRE(bool(node.Forward(bottoms, tops)));
    const auto after = GetInferencePipelineMetrics().Snapshot();
    CHECK(after.rknn_rga_bound_input_normalize_bypasses ==
          before.rknn_rga_bound_input_normalize_bypasses + 1);
    CHECK(after.rknn_native_input_map_calls == before.rknn_native_input_map_calls);
    CHECK(target.frame_ready);
    CHECK(output[0] == 42);
    CHECK(output[bytes - 1] == 42);
}

TEST_CASE("RKNN RGA preprocessing performs centered RGB letterbox on host buffers",
          "[nn][rknn][rga][fast-preprocess]") {
    using namespace cosmo::nn;
    ScopedEnvironment enable("COSMO_RKNN_FAST_PREPROCESS", "1");
    ScopedEnvironment no_force_fail("COSMO_RKNN_RGA_FORCE_FAIL", "0");

    Resize resize;
    resize.dsize   = {640, 640};
    resize.gravity = 1;
    resize.color   = {114, 114, 114};
    SharedResource resource;
    RknnResizeNode node;
    node.SetSharedResource(&resource);
    node.LoadParam(&resize);
    REQUIRE(bool(node.InferTopShapes()));

    auto bottom = std::make_shared<Blob>(PackedImageDesc(720, 1280, IMAGE_BGR), true);
    BlobDesc top_desc;
    top_desc.device_type = DEVICE_NAIVE;
    top_desc.data_type   = node.GetTopBlobDataTypes().front();
    top_desc.dims        = node.GetTopBlobShapes().front();
    auto top             = std::make_shared<Blob>(top_desc, true);
    auto* source         = static_cast<uint8_t*>(bottom->GetHandle().base);
    for (size_t pixel = 0; pixel < static_cast<size_t>(720) * 1280; ++pixel) {
        source[pixel * 3]     = 10;
        source[pixel * 3 + 1] = 20;
        source[pixel * 3 + 2] = 30;
    }

    const auto before = GetInferencePipelineMetrics().Snapshot();
    std::vector<std::shared_ptr<Blob>> bottoms{bottom};
    std::vector<std::shared_ptr<Blob>> tops{top};
    REQUIRE(bool(node.Forward(bottoms, tops)));
    const auto after = GetInferencePipelineMetrics().Snapshot();
    CHECK(after.rknn_rga_fill_calls == before.rknn_rga_fill_calls + 1);
    CHECK(after.rknn_rga_resize_color_calls == before.rknn_rga_resize_color_calls + 1);
    CHECK(after.rknn_rga_failures == before.rknn_rga_failures);
    CHECK(after.rknn_cpu_resize_fallback_calls == before.rknn_cpu_resize_fallback_calls);
    CHECK(top->GetBlobDesc().image_format == IMAGE_RGB);
    CHECK(top->GetBlobDesc().data_format == DATA_FORMAT_NHWC);
    const auto* output = static_cast<const uint8_t*>(top->GetHandle().base);
    CHECK(output[(10 * 640 + 320) * 3] == 114);
    const size_t center = (static_cast<size_t>(320) * 640 + 320) * 3;
    CHECK(output[center] == 30);
    CHECK(output[center + 1] == 20);
    CHECK(output[center + 2] == 10);
}

TEST_CASE("RKNN classifier crop-resize stages extreme scaling in RGA and emits packed RGB",
          "[nn][rknn][rga][crop-resize]") {
    using namespace cosmo::nn;
    ScopedEnvironment no_force_fail("COSMO_RKNN_RGA_FORCE_FAIL", "0");
    CropResize crop;
    crop.h_top_crop    = {0.0f};
    crop.h_bottom_crop = {0.0f};
    crop.w_left_crop   = {0.0f};
    crop.w_right_crop  = {0.0f};
    crop.dsize         = {224, 224};
    crop.gravity       = 0;
    crop.color         = {114, 114, 114};

    SharedResource resource;
    RknnCropResizeNode node;
    node.SetSharedResource(&resource);
    node.LoadParam(&crop);
    REQUIRE(bool(node.InferTopShapes()));

    auto image   = std::make_shared<Blob>(PackedImageDesc(8, 8, IMAGE_BGR), true);
    auto* pixels = static_cast<uint8_t*>(image->GetHandle().base);
    for (size_t pixel = 0; pixel < 64; ++pixel) {
        pixels[pixel * 3]     = 10;
        pixels[pixel * 3 + 1] = 20;
        pixels[pixel * 3 + 2] = 30;
    }
    BlobDesc rect_desc;
    rect_desc.device_type = DEVICE_NAIVE;
    rect_desc.data_type   = DATA_TYPE_INT32;
    rect_desc.dims        = {1, 4};
    auto rect             = std::make_shared<Blob>(rect_desc, true);
    auto* rect_data       = static_cast<int32_t*>(rect->GetHandle().base);
    const std::array<int32_t, 4> crop_rect{2, 2, 4, 4};
    std::copy(crop_rect.begin(), crop_rect.end(), rect_data);

    BlobDesc top_desc;
    top_desc.device_type = DEVICE_NAIVE;
    top_desc.data_type   = DATA_TYPE_UINT8;
    top_desc.data_format = DATA_FORMAT_NHWC;
    top_desc.dims        = node.GetTopBlobShapes().front();
    auto top             = std::make_shared<Blob>(top_desc, true);
    const auto before    = GetInferencePipelineMetrics().Snapshot();
    std::vector<std::shared_ptr<Blob>> images{image};
    std::vector<std::shared_ptr<Blob>> rects{rect};
    std::vector<std::shared_ptr<Blob>> tops{top};
    REQUIRE(bool(node.Forward(images, rects, tops)));
    const auto after = GetInferencePipelineMetrics().Snapshot();
    CHECK(after.rknn_rga_crop_resize_calls == before.rknn_rga_crop_resize_calls + 2);
    CHECK(after.rknn_rga_crop_resize_failures == before.rknn_rga_crop_resize_failures);
    CHECK(after.rknn_rga_crop_host_fallbacks == before.rknn_rga_crop_host_fallbacks + 1);
    CHECK(after.rknn_cpu_crop_resize_fallback_calls == before.rknn_cpu_crop_resize_fallback_calls);
    CHECK(top->GetBlobDesc().image_format == IMAGE_RGB);
    const auto* output = static_cast<const uint8_t*>(top->GetHandle().base);
    CHECK(output[0] == 30);
    CHECK(output[1] == 20);
    CHECK(output[2] == 10);
}

TEST_CASE("RKNN classifier crop-resize keeps an exact CPU fallback", "[nn][rknn][rga][crop-resize]") {
    using namespace cosmo::nn;
    ScopedEnvironment force_fail("COSMO_RKNN_RGA_FORCE_FAIL", "1");
    CropResize crop;
    crop.h_top_crop    = {0.0f};
    crop.h_bottom_crop = {0.0f};
    crop.w_left_crop   = {0.0f};
    crop.w_right_crop  = {0.0f};
    crop.dsize         = {4, 4};
    crop.gravity       = 0;

    SharedResource resource;
    RknnCropResizeNode node;
    node.SetSharedResource(&resource);
    node.LoadParam(&crop);
    REQUIRE(bool(node.InferTopShapes()));
    auto image   = std::make_shared<Blob>(PackedImageDesc(4, 4, IMAGE_BGR), true);
    auto* pixels = static_cast<uint8_t*>(image->GetHandle().base);
    for (size_t pixel = 0; pixel < 16; ++pixel) {
        pixels[pixel * 3]     = 10;
        pixels[pixel * 3 + 1] = 20;
        pixels[pixel * 3 + 2] = 30;
    }
    BlobDesc rect_desc;
    rect_desc.device_type = DEVICE_NAIVE;
    rect_desc.data_type   = DATA_TYPE_INT32;
    rect_desc.dims        = {1, 4};
    auto rect             = std::make_shared<Blob>(rect_desc, true);
    auto* rect_data       = static_cast<int32_t*>(rect->GetHandle().base);
    rect_data[0]          = 0;
    rect_data[1]          = 0;
    rect_data[2]          = 4;
    rect_data[3]          = 4;
    BlobDesc top_desc;
    top_desc.device_type = DEVICE_NAIVE;
    top_desc.data_type   = DATA_TYPE_UINT8;
    top_desc.dims        = node.GetTopBlobShapes().front();
    auto top             = std::make_shared<Blob>(top_desc, true);

    const auto before = GetInferencePipelineMetrics().Snapshot();
    std::vector<std::shared_ptr<Blob>> images{image};
    std::vector<std::shared_ptr<Blob>> rects{rect};
    std::vector<std::shared_ptr<Blob>> tops{top};
    REQUIRE(bool(node.Forward(images, rects, tops)));
    const auto after = GetInferencePipelineMetrics().Snapshot();
    CHECK(after.rknn_rga_crop_resize_failures == before.rknn_rga_crop_resize_failures + 1);
    CHECK(after.rknn_cpu_crop_resize_fallback_calls == before.rknn_cpu_crop_resize_fallback_calls + 1);
    for (int frame = 0; frame < 5; ++frame)
        REQUIRE(bool(node.Forward(images, rects, tops)));
    const auto repeated = GetInferencePipelineMetrics().Snapshot();
    CHECK(repeated.rknn_rga_crop_resize_failures == after.rknn_rga_crop_resize_failures);
    CHECK(repeated.rknn_cpu_crop_resize_fallback_calls == after.rknn_cpu_crop_resize_fallback_calls + 5);
    CHECK(top->GetBlobDesc().data_format == DATA_FORMAT_NHWC);
    CHECK(top->GetBlobDesc().image_format == IMAGE_BGR);
    const auto* output = static_cast<const uint8_t*>(top->GetHandle().base);
    CHECK(output[0] == 10);
    CHECK(output[1] == 20);
    CHECK(output[2] == 30);
}

TEST_CASE("RKNN native input failure is bounded while host RGA keeps processing frames",
          "[nn][rknn][rga][mpp-dmabuf]") {
    using namespace cosmo::nn;
    ScopedEnvironment enable_native("COSMO_RKNN_MPP_DMABUF", "1");
    ScopedEnvironment no_force_fail("COSMO_RKNN_RGA_FORCE_FAIL", "0");
    // Exercise both deterministic import failure and real full-range BT.709
    // CSC. Some supported runtimes accept CSC; others must fall back only once.
    const bool force_import_failure = GENERATE(true, false);
    ScopedEnvironment native_failure("COSMO_RKNN_MPP_DMABUF_FORCE_FAIL", force_import_failure ? "1" : "0");
    struct NativeBuffer {
        MppBufferGroup group{nullptr};
        MppBuffer buffer{nullptr};
        ~NativeBuffer() {
            if (buffer)
                mpp_buffer_put(buffer);
            if (group)
                mpp_buffer_group_put(group);
        }
    } native;
    constexpr int width    = 640;
    constexpr int height   = 320;
    constexpr size_t bytes = width * height * 3 / 2;
    REQUIRE(mpp_buffer_group_get_internal(&native.group, MPP_BUFFER_TYPE_DRM) == MPP_OK);
    REQUIRE(mpp_buffer_get(native.group, &native.buffer, bytes) == MPP_OK);
    REQUIRE(mpp_buffer_sync_begin(native.buffer) == MPP_OK);
    auto* yuv = static_cast<uint8_t*>(mpp_buffer_get_ptr(native.buffer));
    REQUIRE(yuv != nullptr);
    std::fill(yuv, yuv + bytes, 128);
    REQUIRE(mpp_buffer_sync_end(native.buffer) == MPP_OK);

    auto host   = std::make_shared<Blob>(PackedImageDesc(height, width, IMAGE_RGB), true);
    auto handle = host->GetHandle();
    std::fill_n(static_cast<uint8_t*>(handle.base), width * height * 3, 128);
    handle.native_image = {mpp_buffer_get_fd(native.buffer),
                           bytes,
                           width,
                           height,
                           width,
                           height,
                           IMAGE_NV12,
                           NativeImageColorSpace::Bt709,
                           NativeImageColorRange::Full};
    auto image          = std::make_shared<Blob>(host->GetBlobDesc(), handle);
    SharedResource resource;
    RknnResizeNode resize_node;
    RknnCropResizeNode crop_node;
    BlobDesc rect_desc;
    rect_desc.device_type = DEVICE_NAIVE;
    rect_desc.data_type   = DATA_TYPE_INT32;
    rect_desc.dims        = {1, 4};
    auto rect             = std::make_shared<Blob>(rect_desc, true);
    const std::array<int32_t, 4> bounds{0, 0, width, height};
    std::copy(bounds.begin(), bounds.end(), static_cast<int32_t*>(rect->GetHandle().base));
    bool crop_test = false;
    SECTION("detector letterbox") {
        Resize param;
        param.dsize   = {640, 640};
        param.gravity = 1;
        param.color   = {114, 114, 114};
        resize_node.SetSharedResource(&resource);
        resize_node.LoadParam(&param);
        REQUIRE(bool(resize_node.InferTopShapes()));
    }
    SECTION("classifier crop") {
        crop_test = true;
        CropResize param;
        param.h_top_crop = param.h_bottom_crop = param.w_left_crop = param.w_right_crop = {0.0f};
        param.dsize                                                                     = {384, 384};
        param.gravity                                                                   = 0;
        crop_node.SetSharedResource(&resource);
        crop_node.LoadParam(&param);
        REQUIRE(bool(crop_node.InferTopShapes()));
    }
    BlobDesc top_desc = PackedImageDesc(crop_test ? 384 : 640, crop_test ? 384 : 640, IMAGE_RGB);
    auto top          = std::make_shared<Blob>(top_desc, true);
    std::vector<std::shared_ptr<Blob>> images{image}, rects{rect}, tops{top};
    const auto before = GetInferencePipelineMetrics().Snapshot();
    for (int frame = 0; frame < 6; ++frame) {
        REQUIRE(bool(crop_test ? crop_node.Forward(images, rects, tops) : resize_node.Forward(images, tops)));
        const auto* output  = static_cast<const uint8_t*>(top->GetHandle().base);
        const size_t center = crop_test ? (192 * 384 + 192) * 3 : (320 * 640 + 320) * 3;
        for (int channel = 0; channel < 3; ++channel)
            CHECK(std::abs(static_cast<int>(output[center + channel]) - 128) <= 2);
    }
    const auto after     = GetInferencePipelineMetrics().Snapshot();
    const auto fallbacks = after.rknn_mpp_dmabuf_fallbacks - before.rknn_mpp_dmabuf_fallbacks;
    CHECK(fallbacks <= 1);
    if (force_import_failure)
        CHECK(fallbacks == 1);
    CHECK(after.rknn_cpu_resize_fallback_calls == before.rknn_cpu_resize_fallback_calls);
    CHECK(after.rknn_cpu_crop_resize_fallback_calls == before.rknn_cpu_crop_resize_fallback_calls);
}

TEST_CASE("RKNN RGA failure falls back once to CPU while preserving native input mapping",
          "[nn][rknn][rga][fast-preprocess]") {
    using namespace cosmo::nn;
    ScopedEnvironment force_fail("COSMO_RKNN_RGA_FORCE_FAIL", "1");
    Resize resize;
    resize.dsize   = {640, 640};
    resize.gravity = 1;
    resize.color   = {114, 114, 114};
    SharedResource resource;
    RknnResizeNode resize_node;
    resize_node.SetSharedResource(&resource);
    resize_node.LoadParam(&resize);
    REQUIRE(bool(resize_node.InferTopShapes()));

    auto bottom = std::make_shared<Blob>(PackedImageDesc(320, 640, IMAGE_BGR), true);
    BlobDesc resized_desc;
    resized_desc.device_type = DEVICE_NAIVE;
    resized_desc.data_type   = resize_node.GetTopBlobDataTypes().front();
    resized_desc.dims        = resize_node.GetTopBlobShapes().front();
    auto resized             = std::make_shared<Blob>(resized_desc, true);
    auto* source             = static_cast<uint8_t*>(bottom->GetHandle().base);
    std::fill(source, source + static_cast<size_t>(320) * 640 * 3, 128);

    const auto before = GetInferencePipelineMetrics().Snapshot();
    std::vector<std::shared_ptr<Blob>> resize_bottoms{bottom};
    std::vector<std::shared_ptr<Blob>> resize_tops{resized};
    REQUIRE(bool(resize_node.Forward(resize_bottoms, resize_tops)));
    const auto resized_metrics = GetInferencePipelineMetrics().Snapshot();
    CHECK(resized_metrics.rknn_rga_failures == before.rknn_rga_failures + 1);
    CHECK(resized_metrics.rknn_cpu_resize_fallback_calls == before.rknn_cpu_resize_fallback_calls + 1);

    // A frame-local fallback used to retry the failed RGA path on every frame.
    // Keep processing frames while verifying that hardware attempts stop.
    for (int frame = 0; frame < 5; ++frame)
        REQUIRE(bool(resize_node.Forward(resize_bottoms, resize_tops)));
    const auto repeated_metrics = GetInferencePipelineMetrics().Snapshot();
    CHECK(repeated_metrics.rknn_rga_failures == resized_metrics.rknn_rga_failures);
    CHECK(repeated_metrics.rknn_cpu_resize_fallback_calls ==
          resized_metrics.rknn_cpu_resize_fallback_calls + 5);

    Normalize normalize;
    normalize.mean   = {0.0f, 0.0f, 0.0f};
    normalize.scale  = 0.00392157f;
    normalize.is_bgr = false;
    RknnNormalizeNode normalize_node;
    normalize_node.SetSharedResource(&resource);
    normalize_node.LoadParam(&normalize);
    REQUIRE(bool(normalize_node.InferTopShapesWithBottoms({resized->GetBlobDesc().dims},
                                                          {resized->GetBlobDesc().data_type})));
    BlobDesc native_desc;
    native_desc.device_type = DEVICE_NAIVE;
    native_desc.data_type   = normalize_node.GetTopBlobDataTypes().front();
    native_desc.dims        = normalize_node.GetTopBlobShapes().front();
    auto native             = std::make_shared<Blob>(native_desc, true);
    std::vector<std::shared_ptr<Blob>> normalize_bottoms{resized};
    std::vector<std::shared_ptr<Blob>> normalize_tops{native};
    REQUIRE(bool(normalize_node.Forward(normalize_bottoms, normalize_tops)));
    CHECK(native->GetBlobDesc().data_type == DATA_TYPE_INT8);
    CHECK(native->GetBlobDesc().data_format == DATA_FORMAT_NHWC);
    CHECK(static_cast<const int8_t*>(native->GetHandle().base)[(320 * 640 + 320) * 3] == 0);
}

#endif
