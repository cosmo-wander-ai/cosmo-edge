#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_HOST_BACKEND

#include <memory>
#include <vector>

#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/device/cpu/cpu_resize_node.h"
#include "nn/device/cpu/cpu_sequence_node.h"

namespace cosmo::test {
namespace {

    enum class ResizeKind { Image, Crop, Sequence };

    std::vector<float> ResizePixels(ResizeKind kind, const std::vector<uint8_t>& pixels, int src_w, int src_h,
                                    int dst_w, int dst_h) {
        using namespace nn;
        std::vector<uint8_t> image;
        for (auto pixel : pixels) {
            image.insert(image.end(), 3, pixel);
        }
        BlobDesc image_desc;
        image_desc.dims         = {1, src_h, src_w, 3};
        image_desc.data_type    = DATA_TYPE_UINT8;
        image_desc.image_format = IMAGE_BGR;
        BlobHandle image_handle{};
        image_handle.base                         = image.data();
        std::vector<std::shared_ptr<Blob>> images = {std::make_shared<Blob>(image_desc, image_handle)};

        std::vector<int32_t> rect = {0, 0, src_w, src_h};
        BlobDesc rect_desc;
        rect_desc.dims      = {1, 4};
        rect_desc.data_type = DATA_TYPE_INT32;
        BlobHandle rect_handle{};
        rect_handle.base                         = rect.data();
        std::vector<std::shared_ptr<Blob>> rects = {std::make_shared<Blob>(rect_desc, rect_handle)};

        std::unique_ptr<Node> node;
        if (kind == ResizeKind::Image) {
            auto resize = std::make_unique<CpuResizeNode>();
            Resize op;
            op.dsize   = {dst_h, dst_w};
            op.gravity = 0;
            resize->LoadParam(&op);
            node = std::move(resize);
        } else if (kind == ResizeKind::Crop) {
            auto resize = std::make_unique<CpuCropResizeNode>();
            CropResize op;
            op.dsize      = {dst_h, dst_w};
            op.gravity    = 0;
            op.h_top_crop = op.h_bottom_crop = op.w_left_crop = op.w_right_crop = {0.0f};
            resize->LoadParam(&op);
            node = std::move(resize);
        } else {
            auto resize = std::make_unique<CpuSequenceNode>();
            Sequence op;
            op.size  = 1;
            op.dsize = {dst_h, dst_w};
            resize->LoadParam(&op);
            node = std::move(resize);
        }
        SharedResource resource(0);
        node->SetSharedResource(&resource);
        REQUIRE(static_cast<int>(node->InferTopShapes()) == COSMO_NN_OK);

        const int count = dst_w * dst_h;
        std::vector<uint8_t> byte_output(count * 3);
        std::vector<float> float_output(count * 3);
        BlobDesc output_desc;
        output_desc.dims      = node->GetTopBlobShapes().at(0);
        output_desc.data_type = node->GetTopBlobDataTypes().at(0);
        BlobHandle output_handle{};
        output_handle.base = kind == ResizeKind::Sequence ? static_cast<void*>(float_output.data())
                                                          : static_cast<void*>(byte_output.data());
        std::vector<std::shared_ptr<Blob>> outputs = {std::make_shared<Blob>(output_desc, output_handle)};
        auto status = kind == ResizeKind::Image ? node->Forward(images, outputs)
                                                : node->Forward(images, rects, outputs);
        REQUIRE(static_cast<int>(status) == COSMO_NN_OK);

        std::vector<float> result;
        for (int i = 0; i < count; ++i) {
            result.push_back(kind == ResizeKind::Sequence ? float_output[i] * 255.0f : byte_output[i * 3]);
        }
        return result;
    }

}  // namespace

TEST_CASE("CPU resize nodes replicate borders during bilinear interpolation", "[nn][cpu-resize]") {
    auto kind = GENERATE(ResizeKind::Image, ResizeKind::Crop, ResizeKind::Sequence);
    CAPTURE(static_cast<int>(kind));
    std::vector<float> actual;
    std::vector<float> expected;
    SECTION("Horizontal upsampling preserves the left border") {
        actual   = ResizePixels(kind, {100, 200, 100, 200}, 2, 2, 4, 2);
        expected = {100, 125, 175, 200, 100, 125, 175, 200};
    }
    SECTION("Vertical upsampling preserves the top border") {
        actual   = ResizePixels(kind, {100, 100, 200, 200}, 2, 2, 2, 4);
        expected = {100, 100, 125, 125, 175, 175, 200, 200};
    }
    SECTION("A single source pixel fills the destination") {
        actual = ResizePixels(kind, {137}, 1, 1, 3, 3);
        expected.assign(9, 137);
    }
    SECTION("Resizing to the same dimensions preserves pixels") {
        actual   = ResizePixels(kind, {20, 80, 140, 200}, 2, 2, 2, 2);
        expected = {20, 80, 140, 200};
    }
    SECTION("Downsampling averages adjacent pixels") {
        actual   = ResizePixels(kind, {100, 120, 160, 200, 100, 120, 160, 200}, 4, 2, 2, 1);
        expected = {110, 180};
    }
    REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        REQUIRE(actual[i] == Catch::Approx(expected[i]).margin(0.001f));
    }
}

}  // namespace cosmo::test

#endif
