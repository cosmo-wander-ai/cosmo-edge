#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/node/yolo_obb_decode_node.h"
#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {
namespace {

    constexpr float kPi = 3.14159265358979323846f;

    struct Tensor {
        std::vector<float> values;
        std::shared_ptr<Blob> blob;

        Tensor(DimsVector dims, std::vector<float> data) : values(std::move(data)) {
            BlobDesc desc;
            desc.dims = std::move(dims);
            BlobHandle handle;
            handle.base = values.data();
            blob        = std::make_shared<Blob>(desc, handle);
        }
    };

    struct Decoder {
        YoloObbDecodeNode node;
        Tensor output{{2, 4, 10}, std::vector<float>(80)};

        explicit Decoder(bool normalized = false) {
            YoloPost op;
            op.top_k                  = 4;
            op.nms_detection_conf     = 0.1f;
            op.input_width            = 200;
            op.input_height           = 100;
            op.normalized_coordinates = normalized;
            node.SetMaxBatch(2);
            node.LoadParam(&op);
            REQUIRE((node.InferTopShapes() == COSMO_NN_OK));
        }

        Status Forward(Tensor& input) {
            std::vector<std::shared_ptr<Blob>> bottoms{input.blob};
            std::vector<std::shared_ptr<Blob>> tops{output.blob};
            return node.Forward(bottoms, tops);
        }

        std::vector<std::vector<ObjectInfoV1>> Parse(std::vector<ObbResizeTransform> transforms = {{}}) {
            std::vector<std::vector<ObjectInfoV1>> result;
            REQUIRE((ParseYoloObbOutput(output.blob, transforms, {}, {}, {}, result) == COSMO_NN_OK));
            return result;
        }
    };

    TEST_CASE("OBB decoder derives source quad and enclosing AABB at zero, 45 and 90 degrees", "[obb]") {
        const float angle = GENERATE(0.f, kPi / 4.f, kPi / 2.f);
        Tensor input{{1, 1, 7}, {100.f, 100.f, 160.f, 20.f, 0.9f, 2.f, angle}};
        Decoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        auto result = decoder.Parse();
        REQUIRE(result.size() == 1);
        REQUIRE(result[0].size() == 1);
        const auto& object = result[0][0];
        REQUIRE(object.oriented_corners.has_value());
        const float half_width  = 80.f * std::abs(std::cos(angle)) + 10.f * std::abs(std::sin(angle));
        const float half_height = 80.f * std::abs(std::sin(angle)) + 10.f * std::abs(std::cos(angle));
        REQUIRE(object.x1 == Catch::Approx(100.f - half_width).margin(0.0001f));
        REQUIRE(object.x2 == Catch::Approx(100.f + half_width).margin(0.0001f));
        REQUIRE(object.y1 == Catch::Approx(100.f - half_height).margin(0.0001f));
        REQUIRE(object.y2 == Catch::Approx(100.f + half_height).margin(0.0001f));
        REQUIRE(object.infos[0].class_id == 2);
        REQUIRE(object.infos[0].class_name == "class_2");
    }

    TEST_CASE("OBB border vertices remain intact instead of clipping an unrotated rectangle", "[obb]") {
        Tensor input{{1, 1, 7}, {5.f, 50.f, 160.f, 20.f, 0.9f, 0.f, kPi / 2.f}};
        Decoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        const auto object = decoder.Parse()[0][0];
        REQUIRE(object.x1 == Catch::Approx(-5.f).margin(0.0001f));
        REQUIRE(object.x2 == Catch::Approx(15.f).margin(0.0001f));
        REQUIRE(object.y1 == Catch::Approx(-30.f).margin(0.0001f));
        REQUIRE(object.y2 == Catch::Approx(130.f).margin(0.0001f));
    }

    TEST_CASE("OBB inverse stretch transforms vertices independently on non-square source", "[obb]") {
        ObbResizeTransform transform;
        REQUIRE((pipeline_utils::MakeObbResizeTransform({200, 100}, {100, 100}, 0, DEVICE_CPU, transform) ==
                 COSMO_NN_OK));
        Tensor input{{1, 1, 7}, {50.f, 50.f, 40.f, 20.f, 0.9f, 0.f, kPi / 4.f}};
        Decoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        const auto object = decoder.Parse({transform})[0][0];
        const auto& q     = *object.oriented_corners;
        REQUIRE(q[0].x == Catch::Approx(100.f - 10.f * std::sqrt(2.f)));
        REQUIRE(q[0].y == Catch::Approx(50.f - 15.f * std::sqrt(2.f)));
        REQUIRE(object.x2 - object.x1 == Catch::Approx(60.f * std::sqrt(2.f)));
        REQUIRE(object.y2 - object.y1 == Catch::Approx(30.f * std::sqrt(2.f)));
    }

    TEST_CASE("OBB letterbox undo preserves a ninety-degree narrow box", "[obb]") {
        ObbResizeTransform transform;
        REQUIRE((pipeline_utils::MakeObbResizeTransform({1920, 1080}, {640, 640}, 1, DEVICE_CPU, transform) ==
                 COSMO_NN_OK));
        Tensor input{{1, 1, 7}, {320.f, 320.f, 160.f, 20.f, 0.9f, 0.f, kPi / 2.f}};
        Decoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        const auto object = decoder.Parse({transform})[0][0];
        REQUIRE(object.x1 == Catch::Approx(930.f));
        REQUIRE(object.x2 == Catch::Approx(990.f));
        REQUIRE(object.y1 == Catch::Approx(300.f));
        REQUIRE(object.y2 == Catch::Approx(780.f));
    }

    TEST_CASE("OBB inverse resize matches CPU truncation and Sophon integer rounding", "[obb]") {
        ObbResizeTransform cpu, sophon;
        REQUIRE((pipeline_utils::MakeObbResizeTransform({1001, 668}, {320, 256}, 1, DEVICE_CPU, cpu) ==
                 COSMO_NN_OK));
        REQUIRE((pipeline_utils::MakeObbResizeTransform({1001, 668}, {320, 256}, 1, DEVICE_SOPHON_TPU,
                                                        sophon) == COSMO_NN_OK));
        REQUIRE(cpu.scale_x == Catch::Approx(320.f / 1001.f));
        REQUIRE(cpu.scale_y == Catch::Approx(213.f / 668.f));
        REQUIRE(sophon.scale_y == Catch::Approx(214.f / 668.f));
        REQUIRE(cpu.offset_y == 21.f);
        REQUIRE(sophon.offset_y == 21.f);
    }

    TEST_CASE("OBB resize configuration gates Sophon alignment without restricting CPU width", "[obb]") {
        REQUIRE((pipeline_utils::ValidateObbResizeConfig({672, 640}, DEVICE_CPU) == COSMO_NN_OK));
        REQUIRE((pipeline_utils::ValidateObbResizeConfig({672, 640}, DEVICE_SOPHON_TPU) ==
                 COSMO_NN_ERR_INVALID_CFG));
        REQUIRE((pipeline_utils::ValidateObbResizeConfig({704, 640}, DEVICE_SOPHON_TPU) == COSMO_NN_OK));
        ObbResizeTransform transform;
        REQUIRE((pipeline_utils::MakeObbResizeTransform({1920, 1080}, {672, 640}, 1, DEVICE_SOPHON_TPU,
                                                        transform) == COSMO_NN_ERR_INVALID_CFG));
        REQUIRE((pipeline_utils::MakeObbResizeTransform({1920, 1080}, {672, 640}, 1, DEVICE_CPU, transform) ==
                 COSMO_NN_OK));
    }

    TEST_CASE("OBB inverse gravity two matches selected backend crop or padding", "[obb]") {
        ObbResizeTransform cpu, sophon;
        REQUIRE((pipeline_utils::MakeObbResizeTransform({640, 360}, {320, 320}, 2, DEVICE_CPU, cpu) ==
                 COSMO_NN_OK));
        REQUIRE(cpu.scale_x == 0.5f);
        REQUIRE(cpu.scale_y == 0.5f);
        REQUIRE(cpu.offset_x == 0.f);
        REQUIRE(cpu.offset_y == 0.f);
        REQUIRE((pipeline_utils::MakeObbResizeTransform({640, 360}, {320, 320}, 2, DEVICE_SOPHON_TPU,
                                                        sophon) == COSMO_NN_OK));
        REQUIRE(sophon.scale_x == Catch::Approx(320.f / 360.f));
        REQUIRE(sophon.offset_x == Catch::Approx(-140.f * 320.f / 360.f));
        REQUIRE(sophon.offset_y == 0.f);
        Tensor input{{1, 1, 7}, {160.f, 160.f, 100.f, 20.f, 0.9f, 0.f, 0.f}};
        Decoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        auto object = decoder.Parse({sophon})[0][0];
        REQUIRE((object.x1 + object.x2) / 2 == Catch::Approx(320.f));
        REQUIRE((object.y1 + object.y2) / 2 == Catch::Approx(180.f));
    }

    TEST_CASE("OBB coordinate units are explicit even for subpixel boxes", "[obb]") {
        Tensor input{{1, 1, 7}, {0.5f, 0.5f, 0.2f, 0.4f, 0.9f, 0.f, 0.f}};
        Decoder pixels;
        REQUIRE((pixels.Forward(input) == COSMO_NN_OK));
        const auto small = pixels.Parse()[0][0];
        REQUIRE(small.x1 == Catch::Approx(0.4f));
        REQUIRE(small.y2 == Catch::Approx(0.7f));
        Decoder normalized(true);
        REQUIRE((normalized.Forward(input) == COSMO_NN_OK));
        const auto scaled = normalized.Parse()[0][0];
        REQUIRE(scaled.x1 == Catch::Approx(80.f));
        REQUIRE(scaled.y2 == Catch::Approx(70.f));
    }

    TEST_CASE("OBB malformed tensors fail before indexing or writing", "[obb]") {
        Decoder decoder;
        for (const auto& dims :
             std::vector<DimsVector>{{7}, {1, 7}, {1, 1, 6}, {1, 1, 8}, {1, 1, 7, 1}, {1, 0, 7}, {3, 1, 7}}) {
            Tensor bad{dims, std::vector<float>(32)};
            CHECK((decoder.Forward(bad) != COSMO_NN_OK));
        }
        Tensor input{{1, 1, 7}, {1.f, 2.f, 3.f, 4.f, 0.9f, 0.f, 0.f}};
        input.blob->GetBlobDesc().data_type = DATA_TYPE_INT32;
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
        input.blob->GetBlobDesc().data_type          = DATA_TYPE_FLOAT;
        decoder.output.blob->GetBlobDesc().data_type = DATA_TYPE_HALF;
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
        std::vector<std::shared_ptr<Blob>> none;
        CHECK((decoder.node.Forward(none, none) != COSMO_NN_OK));
    }

    TEST_CASE("OBB rejects nonfinite and invalid rows but retains valid rows", "[obb]") {
        for (int field = 0; field < 7; ++field) {
            for (float invalid :
                 {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
                Tensor input{{1, 2, 7}, {50, 50, 40, 20, 0.9f, 0, 0, 50, 50, 40, 20, 0.9f, 1, 0}};
                input.values[field] = invalid;
                Decoder decoder;
                REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
                const auto result = decoder.Parse();
                REQUIRE(result[0].size() == 1);
                REQUIRE(result[0][0].infos[0].class_id == 1);
            }
        }
        for (const auto& change : std::vector<std::pair<int, float>>{
                 {2, 0}, {3, -1}, {4, 1.5f}, {5, -1}, {5, 0.5f}, {5, 2147483648.f}}) {
            Tensor input{{1, 1, 7}, {50, 50, 40, 20, 0.9f, 0, 0}};
            input.values[change.first] = change.second;
            Decoder decoder;
            REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
            REQUIRE(decoder.Parse()[0].empty());
        }
    }

    TEST_CASE("OBB actual image batch and selected class thresholds are honored", "[obb]") {
        Decoder decoder;
        Tensor input{{1, 3, 7},
                     {50, 50, 40, 20, 0.7f, 1, 0, 50, 50, 40, 20, 0.9f, 2, 0, 50, 50, 40, 20, 0.95f, 1, 0}};
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        std::vector<std::vector<ObjectInfoV1>> result;
        REQUIRE(
            (ParseYoloObbOutput(decoder.output.blob, {{}}, {1}, {0.8f}, {"target"}, result) == COSMO_NN_OK));
        REQUIRE(result.size() == 1);
        REQUIRE(result[0].size() == 1);
        REQUIRE(result[0][0].infos[0].class_name == "target");
        Tensor next{{2, 1, 7}, {50, 50, 40, 20, 0.9f, 0, 0, 70, 70, 40, 20, 0.9f, 0, 0}};
        REQUIRE((decoder.Forward(next) == COSMO_NN_OK));
        auto batch = decoder.Parse({{}, {}});
        REQUIRE(batch.size() == 2);
        REQUIRE(batch[0].size() == 1);
        REQUIRE(batch[1].size() == 1);
        REQUIRE(batch[1][0].x1 == Catch::Approx(50.f));
    }

    TEST_CASE("OBB configured fallback confidence applies when labels are absent", "[obb]") {
        Tensor input{{1, 1, 7}, {50, 50, 40, 20, 0.15f, 1, 0}};
        Decoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        std::vector<std::vector<ObjectInfoV1>> result;
        REQUIRE((ParseYoloObbOutput(decoder.output.blob, {{}}, {}, {}, {}, result, 0.1f) == COSMO_NN_OK));
        REQUIRE(result[0].size() == 1);
        REQUIRE((ParseYoloObbOutput(decoder.output.blob, {{}}, {}, {}, {}, result, 0.2f) == COSMO_NN_OK));
        REQUIRE(result[0].empty());
    }

    TEST_CASE("OBB rejects empty or invalid transforms", "[obb]") {
        ObbResizeTransform transform;
        REQUIRE((pipeline_utils::MakeObbResizeTransform({0, 480}, {640, 640}, 1, DEVICE_CPU, transform) !=
                 COSMO_NN_OK));
        REQUIRE((pipeline_utils::MakeObbResizeTransform({640, 480}, {0, 640}, 1, DEVICE_CPU, transform) !=
                 COSMO_NN_OK));
        REQUIRE((pipeline_utils::MakeObbResizeTransform({640, 480}, {640, 640}, 3, DEVICE_CPU, transform) !=
                 COSMO_NN_OK));
        Decoder decoder;
        std::vector<std::vector<ObjectInfoV1>> output;
        REQUIRE((ParseYoloObbOutput(decoder.output.blob, {}, {}, {}, {}, output) != COSMO_NN_OK));
        REQUIRE((ParseYoloObbOutput(decoder.output.blob, {{0, 1, 0, 0}}, {}, {}, {}, output) != COSMO_NN_OK));
    }

    TEST_CASE("Ordinary detection parser does not infer OBB from a seventh column", "[obb]") {
        Tensor input{{1, 1, 7}, {50, 50, 40, 20, 0.9f, 0, 0.7f}};
        std::vector<std::vector<ObjectInfoV1>> result;
        REQUIRE((NetUtils::PickDetectionObjects(input.blob, {{100, 100}}, {100, 100}, {}, {}, {}, result) ==
                 COSMO_NN_OK));
        REQUIRE(result.size() == 1);
        REQUIRE(result[0].size() == 1);
        REQUIRE_FALSE(result[0][0].oriented_corners.has_value());
        REQUIRE(result[0][0].angle == 0.f);
        REQUIRE(result[0][0].x1 == 30.f);
        REQUIRE(result[0][0].y1 == 40.f);
    }

}  // namespace
}  // namespace cosmo::nn
