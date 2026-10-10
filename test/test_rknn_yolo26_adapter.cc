#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <cmath>
#include <string>
#include <vector>

#include "nn/device/rknn/rknn_yolo26_adapter.h"
#include "nn/device/rknn/rknn_yolov8_adapter.h"

TEST_CASE("RKNN YOLO26 six heads reconstruct end-to-end detections", "[nn][rknn][yolo26]") {
    using namespace cosmo::nn;
    const std::vector<std::vector<int>> shapes{
        {1, 4, 4, 4}, {1, 2, 4, 4}, {1, 4, 2, 2}, {1, 2, 2, 2}, {1, 4, 1, 1}, {1, 2, 1, 1},
    };
    RknnOutputAdapterContract contract;
    std::string error;
    REQUIRE(ResolveRknnOutputAdapter(shapes, contract, error));
    CHECK(contract.kind == RknnOutputAdapterKind::Yolo26OneToOne6HeadV1);
    CHECK(contract.logical_shape == std::vector<int>{1, 300, 6});
    CHECK(std::string(RknnOutputAdapterName(contract.kind)) == "yolo26_one2one_6head_v1");

    std::vector<std::vector<float>> values;
    values.reserve(shapes.size());
    for (size_t index = 0; index < shapes.size(); ++index) {
        const auto& shape = shapes[index];
        values.emplace_back(static_cast<size_t>(shape[1] * shape[2] * shape[3]),
                            index % 2 == 0 ? 1.0f : -20.0f);
    }
    values[1][0]     = 3.0f;  // Fine-scale anchor 0, class 0.
    values[3][4 + 0] = 2.0f;  // Mid-scale anchor 0, class 1.
    std::vector<RknnYolo26Head> heads;
    for (size_t index = 0; index < shapes.size(); ++index)
        heads.push_back({values[index].data(), values[index].size(), shapes[index]});

    std::vector<float> output(300 * 6);
    REQUIRE(ReconstructRknnYolo26(heads, 32, 32, output.data(), output.size(), error));
    CHECK(output[0] == Catch::Approx(-4.0f));
    CHECK(output[1] == Catch::Approx(-4.0f));
    CHECK(output[2] == Catch::Approx(12.0f));
    CHECK(output[3] == Catch::Approx(12.0f));
    CHECK(output[4] == Catch::Approx(1.0f / (1.0f + std::exp(-3.0f))));
    CHECK(output[5] == 0.0f);
    CHECK(output[10] == Catch::Approx(1.0f / (1.0f + std::exp(-2.0f))));
    CHECK(output[11] == 1.0f);
}

TEST_CASE("RKNN YOLO26 layout rejects DFL and malformed heads", "[nn][rknn][yolo26]") {
    using namespace cosmo::nn;
    std::string error;
    RknnYolo26Layout layout;
    const std::vector<std::vector<int>> dfl{
        {1, 64, 4, 4}, {1, 80, 4, 4}, {1, 64, 2, 2}, {1, 80, 2, 2}, {1, 64, 1, 1}, {1, 80, 1, 1},
    };
    CHECK_FALSE(DetectRknnYolo26Layout(dfl, layout, error));
    CHECK_FALSE(error.empty());
}

#endif  // COSMO_NN_USE_RKNN_BACKEND
