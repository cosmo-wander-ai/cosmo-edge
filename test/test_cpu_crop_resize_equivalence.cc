#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_HOST_BACKEND

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <vector>

#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/utils/op.h"

namespace {
using namespace cosmo::nn;

// Preserved reference for the pre-optimization two-copy algorithm. Its input
// is a tightly copied ROI, independent of the new strided implementation.
void LegacyResize(const std::vector<uint8_t>& source, int sw, int sh, uint8_t* dst, int dw, int dh) {
    const float xr = static_cast<float>(sw) / dw, yr = static_cast<float>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        float fy = std::clamp((y + 0.5f) * yr - 0.5f, 0.0f, static_cast<float>(sh - 1));
        int sy   = static_cast<int>(fy);
        float dy = fy - sy;
        sy       = std::max(0, std::min(sy, sh - 1));
        int sy1  = std::min(sy + 1, sh - 1);
        for (int x = 0; x < dw; ++x) {
            float fx = std::clamp((x + 0.5f) * xr - 0.5f, 0.0f, static_cast<float>(sw - 1));
            int sx   = static_cast<int>(fx);
            float dx = fx - sx;
            sx       = std::max(0, std::min(sx, sw - 1));
            int sx1  = std::min(sx + 1, sw - 1);
            for (int c = 0; c < 3; ++c) {
                float a = source[(sy * sw + sx) * 3 + c], b = source[(sy * sw + sx1) * 3 + c];
                float d = source[(sy1 * sw + sx) * 3 + c], e = source[(sy1 * sw + sx1) * 3 + c];
                float value = a * (1 - dx) * (1 - dy) + b * dx * (1 - dy) + d * (1 - dx) * dy + e * dx * dy;
                dst[(y * dw + x) * 3 + c] =
                    static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, value + 0.5f)));
            }
        }
    }
}

class InspectCrop final : public CpuCropResizeNode {
public:
    const std::vector<int32_t>& Rectangles() const {
        return calculated_rects;
    }
};

std::shared_ptr<Blob> Wrap(BlobDesc desc, void* ptr) {
    BlobHandle handle;
    handle.base = ptr;
    return std::make_shared<Blob>(desc, handle);
}

std::vector<uint8_t> Compare(const std::vector<uint8_t>& pixels, int width, int height,
                             const std::vector<std::array<int32_t, 4>>& boxes, int gravity, bool square,
                             int square_mode = 0, float margin = 0.0f) {
    constexpr int dw = 224, dh = 224;
    InspectCrop node;
    node.SetMaxBatch(boxes.size());
    CropResize op;
    op.dsize       = {dh, dw};
    op.gravity     = gravity;
    op.color       = {114};
    op.square      = square;
    op.square_mode = square_mode;
    op.h_top_crop = op.h_bottom_crop = op.w_left_crop = op.w_right_crop = {margin};
    node.LoadParam(&op);
    BlobDesc image_desc;
    image_desc.device_type  = DEVICE_NAIVE;
    image_desc.data_type    = DATA_TYPE_UINT8;
    image_desc.data_format  = DATA_FORMAT_NHWC;
    image_desc.image_format = IMAGE_BGR;
    image_desc.dims         = {1, height, width, 3};
    BlobDesc rect_desc;
    rect_desc.device_type = DEVICE_NAIVE;
    rect_desc.data_type   = DATA_TYPE_INT32;
    rect_desc.dims        = {static_cast<int>(boxes.size()), 4};
    BlobDesc top_desc;
    top_desc.device_type  = DEVICE_NAIVE;
    top_desc.data_type    = DATA_TYPE_UINT8;
    top_desc.data_format  = DATA_FORMAT_NHWC;
    top_desc.image_format = IMAGE_BGR;
    top_desc.dims         = {static_cast<int>(boxes.size()), dh, dw, 3};
    std::vector<uint8_t> output(boxes.size() * dh * dw * 3, 0x77), expected(output.size(), 0x77);
    std::vector<std::array<int32_t, 4>> mutable_boxes = boxes;
    auto img                                          = Wrap(image_desc, const_cast<uint8_t*>(pixels.data()));
    auto rect                                         = Wrap(rect_desc, mutable_boxes.data());
    auto top                                          = Wrap(top_desc, output.data());
    std::vector<std::shared_ptr<Blob>> images{img}, rects{rect}, tops{top};
    auto status = node.Forward(images, rects, tops);
    INFO(status.description());
    REQUIRE(bool(status));
    const auto& prepared = node.Rectangles();
    REQUIRE(prepared.size() == boxes.size() * 4);
    for (size_t i = 0; i < boxes.size(); ++i) {
        const int x = prepared[4 * i], y = prepared[4 * i + 1], w = prepared[4 * i + 2],
                  h = prepared[4 * i + 3];
        REQUIRE(x >= 0);
        REQUIRE(y >= 0);
        REQUIRE(w > 0);
        REQUIRE(h > 0);
        REQUIRE(x + w <= width);
        REQUIRE(y + h <= height);
        std::vector<uint8_t> crop(static_cast<size_t>(w) * h * 3);
        for (int row = 0; row < h; ++row)
            std::memcpy(crop.data() + static_cast<size_t>(row) * w * 3,
                        pixels.data() + (static_cast<size_t>(y + row) * width + x) * 3,
                        static_cast<size_t>(w) * 3);
        auto* target = expected.data() + i * dh * dw * 3;
        if (gravity == 0)
            LegacyResize(crop, w, h, target, dw, dh);
        else {
            const float scale = std::min(static_cast<float>(dw) / w, static_cast<float>(dh) / h);
            const int nw = static_cast<int>(w * scale), nh = static_cast<int>(h * scale);
            std::memset(target, 114, dh * dw * 3);
            if (nw > 0 && nh > 0) {
                std::vector<uint8_t> resized(static_cast<size_t>(nw) * nh * 3);
                LegacyResize(crop, w, h, resized.data(), nw, nh);
                const int px = gravity == 1 ? (dw - nw) / 2 : 0, py = gravity == 1 ? (dh - nh) / 2 : 0;
                for (int row = 0; row < nh; ++row)
                    std::memcpy(target + (static_cast<size_t>(py + row) * dw + px) * 3,
                                resized.data() + static_cast<size_t>(row) * nw * 3,
                                static_cast<size_t>(nw) * 3);
            }
        }
    }
    CHECK(output == expected);
    return output;
}
}  // namespace

TEST_CASE("CPU crop-resize matches the previous crop and resize bytes", "[nn][crop-resize][equivalence]") {
    constexpr int w = 1920, h = 1080;
    std::mt19937 rng(3588);
    std::vector<uint8_t> image(static_cast<size_t>(w) * h * 3);
    for (auto& byte : image)
        byte = static_cast<uint8_t>(rng() & 255);
    // Coordinates are from the previously captured public pedestrian-scene
    // detections; the default pixel contents are deterministic synthetic data.
    std::vector<std::array<int32_t, 4>> boxes{{462, 572, 80, 224},  {1225, 300, 55, 126}, {883, 417, 72, 181},
                                              {530, 302, 55, 134},  {860, 308, 60, 149},  {0, 0, 1, 1},
                                              {w - 1, h - 1, 1, 1}, {0, 0, w, h},         {1, 1, 3, 5}};
    for (int i = 0; i < 35; ++i) {
        const int x = static_cast<int>(rng() % w), y = static_cast<int>(rng() % h);
        boxes.push_back({x, y, 1 + static_cast<int>(rng() % (w - x)), 1 + static_cast<int>(rng() % (h - y))});
    }
    for (int gravity : {0, 1, 2})
        for (bool square : {false, true})
            Compare(image, w, h, boxes, gravity, square);
    for (int square_mode : {0, 1, 2})
        Compare(image, w, h, boxes, 1, true, square_mode, 0.1f);
    Compare(image, w, h, boxes, 0, false, 0, 0.1f);
    // Optional private public-source frame, supplied by the task harness only.
    // It is not embedded in the repository or required by normal CPU tests.
    if (const char* raw = std::getenv("COSMO_TEST_CROP_BGR_1920X1080")) {
        std::ifstream input(raw, std::ios::binary);
        REQUIRE(input.good());
        input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
        REQUIRE(input.gcount() == static_cast<std::streamsize>(image.size()));
        REQUIRE(input.peek() == std::char_traits<char>::eof());
        for (int gravity : {0, 1, 2})
            for (bool square : {false, true})
                Compare(image, w, h, boxes, gravity, square);
    }
}

TEST_CASE("CPU crop-resize preserves multi-image batching and rejects invalid ROIs",
          "[nn][crop-resize][equivalence]") {
    constexpr int w = 8, h = 8, size = 224 * 224 * 3;
    std::vector<uint8_t> a(w * h * 3), b(w * h * 3);
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] = static_cast<uint8_t>(i);
        b[i] = static_cast<uint8_t>(255 - i);
    }
    std::vector<std::array<int32_t, 4>> boxes_a{{0, 0, 3, 5}, {2, 1, 5, 6}};
    std::vector<std::array<int32_t, 4>> boxes_b{{1, 2, 6, 3}};
    auto expected = Compare(a, w, h, boxes_a, 1, false);
    auto suffix   = Compare(b, w, h, boxes_b, 1, false);
    expected.insert(expected.end(), suffix.begin(), suffix.end());
    InspectCrop node;
    node.SetMaxBatch(3);
    CropResize op;
    op.dsize      = {224, 224};
    op.gravity    = 1;
    op.color      = {114};
    op.h_top_crop = op.h_bottom_crop = op.w_left_crop = op.w_right_crop = {0.0f};
    node.LoadParam(&op);
    BlobDesc img;
    img.device_type  = DEVICE_NAIVE;
    img.data_type    = DATA_TYPE_UINT8;
    img.data_format  = DATA_FORMAT_NHWC;
    img.image_format = IMAGE_BGR;
    img.dims         = {1, h, w, 3};
    BlobDesc ra;
    ra.device_type = DEVICE_NAIVE;
    ra.data_type   = DATA_TYPE_INT32;
    ra.dims        = {2, 4};
    auto rb        = ra;
    rb.dims        = {1, 4};
    auto out_desc  = img;
    out_desc.dims  = {3, 224, 224, 3};
    std::vector<uint8_t> out(3 * size, 0);
    std::vector<std::shared_ptr<Blob>> images{Wrap(img, a.data()), Wrap(img, b.data())};
    std::vector<std::shared_ptr<Blob>> rects{Wrap(ra, boxes_a.data()), Wrap(rb, boxes_b.data())};
    std::vector<std::shared_ptr<Blob>> tops{Wrap(out_desc, out.data())};
    REQUIRE(bool(node.Forward(images, rects, tops)));
    CHECK(out == expected);

    for (auto invalid : {std::array<int32_t, 4>{0, 0, 0, 1}, std::array<int32_t, 4>{20, 0, 2, 2}}) {
        std::vector<std::shared_ptr<Blob>> one_image{images[0]}, one_rect{Wrap(rb, invalid.data())};
        CHECK_FALSE(bool(node.Forward(one_image, one_rect, tops)));
    }
}
#endif
