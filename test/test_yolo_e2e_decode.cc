#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/node/yolo_e2e_decode_node.h"
#include "nn/pipeline/pipeline_utils.h"

namespace cosmo::nn {
namespace {

    struct DecodeTensor {
        std::vector<float> values;
        std::shared_ptr<Blob> blob;

        DecodeTensor(DimsVector dims, std::vector<float> data) : values(std::move(data)) {
            BlobDesc desc;
            desc.dims = std::move(dims);
            BlobHandle handle;
            handle.base = values.data();
            blob        = std::make_shared<Blob>(desc, handle);
        }
    };

    struct E2EDecoder {
        YoloE2EDecodeNode node;
        DecodeTensor output;

        explicit E2EDecoder(bool raw = false, int top_k = 4, float conf = 0.25f, float nms = 0.7f)
            : output({2, top_k, 6}, std::vector<float>(2 * top_k * 6, -99.f)) {
            auto op = pipeline_utils::MakeYoloE2EPostOp(conf, top_k, 200, 100, nms, raw);
            node.SetMaxBatch(2);
            node.LoadParam(op.get());
            REQUIRE((node.InferTopShapes() == COSMO_NN_OK));
        }

        Status Forward(DecodeTensor& input) {
            std::vector<std::shared_ptr<Blob>> bottoms{input.blob};
            std::vector<std::shared_ptr<Blob>> tops{output.blob};
            return node.Forward(bottoms, tops);
        }

        void CheckRow(int batch, int row, const std::array<float, 6>& expected) const {
            const int offset = (batch * output.blob->GetBlobDesc().dims[1] + row) * 6;
            for (int c = 0; c < 6; ++c)
                CHECK(output.values[offset + c] == Catch::Approx(expected[c]));
        }
    };

    std::vector<float> RawRows(const std::vector<std::vector<float>>& rows) {
        const std::size_t cols = rows.front().size();
        std::vector<float> result(cols * rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            REQUIRE(rows[i].size() == cols);
            for (std::size_t c = 0; c < cols; ++c)
                result[c * rows.size() + i] = rows[i][c];
        }
        return result;
    }

    TEST_CASE("YOLO end-to-end factory defaults to decoded rows and raw mode is explicit",
              "[nn][yolo26][decode]") {
        const auto decoded = pipeline_utils::MakeYoloE2EPostOp(0.25f, 4, 200, 100);
        CHECK_FALSE(decoded->raw_output);
        const auto raw = pipeline_utils::MakeYoloE2EPostOp(0.25f, 4, 200, 100, 0.45f, true);
        CHECK(raw->raw_output);
        CHECK(raw->nms_threshold == Catch::Approx(0.45f));
    }

    TEST_CASE("YOLO end-to-end decoded pixel rows retain xyxy conversion without additional NMS",
              "[nn][yolo26][decode]") {
        DecodeTensor input{{1, 2, 6}, {10, 20, 50, 40, 0.9f, 0, 10, 20, 50, 40, 0.8f, 0}};
        E2EDecoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {30, 30, 40, 20, 0.9f, 0});
        decoder.CheckRow(0, 1, {30, 30, 40, 20, 0.8f, 0});
        decoder.CheckRow(0, 2, {0, 0, 0, 0, 0, 0});
        CHECK((decoder.output.blob->GetBlobDesc().dims == DimsVector{2, 4, 6}));
    }

    TEST_CASE("YOLO end-to-end decoded normalized rows retain coordinate conversion",
              "[nn][yolo26][decode]") {
        DecodeTensor input{{1, 1, 6}, {0.1f, 0.2f, 0.5f, 0.6f, 0.9f, 1}};
        E2EDecoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {60, 40, 80, 40, 0.9f, 1});
    }

    TEST_CASE("YOLO end-to-end raw node accepts arbitrary class counts and one candidate",
              "[nn][yolo26][decode]") {
        const int classes = GENERATE(1, 2, 80);
        std::vector<float> row(4 + classes, 0.05f);
        row[0]           = 100;
        row[1]           = 50;
        row[2]           = 40;
        row[3]           = 20;
        row[3 + classes] = 0.9f;
        DecodeTensor input{{1, 4 + classes, 1}, row};
        E2EDecoder decoder(true);
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {100, 50, 40, 20, 0.9f, static_cast<float>(classes - 1)});
    }

    TEST_CASE("YOLO end-to-end raw six-candidate tensor is not mistaken for decoded rows",
              "[nn][yolo26][decode]") {
        DecodeTensor input{{1, 6, 6},
                           RawRows({
                               {100, 50, 40, 20, 0.9f, 0.1f},
                               {101, 50, 40, 20, 0.8f, 0.1f},
                               {100, 50, 40, 20, 0.1f, 0.85f},
                               {150, 50, 10, 10, 0.25f, 0.1f},
                               {160, 50, 10, 10, 0.2f, 0.1f},
                               {180, 50, 10, 10, 0.7f, 0.1f},
                           })};
        E2EDecoder decoder(true);
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {100, 50, 40, 20, 0.9f, 0});
        decoder.CheckRow(0, 1, {100, 50, 40, 20, 0.85f, 1});
        decoder.CheckRow(0, 2, {180, 50, 10, 10, 0.7f, 0});
        decoder.CheckRow(0, 3, {0, 0, 0, 0, 0, 0});
    }

    TEST_CASE("YOLO end-to-end default six-row tensor remains decoded", "[nn][yolo26][decode]") {
        DecodeTensor input{{1, 6, 6},
                           {
                               10, 20, 50, 40, 0.9f, 0, 30, 30, 50, 50, 0.8f, 1, 0, 0, 0, 0, 0, 0,
                               0,  0,  0,  0,  0,    0, 0,  0,  0,  0,  0,    0, 0, 0, 0, 0, 0, 0,
                           }};
        E2EDecoder decoder;
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {30, 30, 40, 20, 0.9f, 0});
        decoder.CheckRow(0, 1, {40, 40, 20, 20, 0.8f, 1});
    }

    TEST_CASE("YOLO end-to-end raw small pixel boxes are never automatically denormalized",
              "[nn][yolo26][decode]") {
        DecodeTensor input{{1, 5, 1}, {0.5f, 0.5f, 0.2f, 0.4f, 0.9f}};
        E2EDecoder decoder(true);
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {0.5f, 0.5f, 0.2f, 0.4f, 0.9f, 0});
    }

    TEST_CASE("YOLO end-to-end batches reuse fixed output capacity and clear padding",
              "[nn][yolo26][decode]") {
        const bool raw = GENERATE(false, true);
        E2EDecoder decoder(raw);
        DecodeTensor single{
            raw ? DimsVector{1, 5, 1} : DimsVector{1, 1, 6},
            raw ? std::vector<float>{30, 30, 40, 20, 0.9f} : std::vector<float>{10, 20, 50, 40, 0.9f, 0}};
        DecodeTensor batch{raw ? DimsVector{2, 5, 1} : DimsVector{2, 1, 6},
                           raw ? std::vector<float>{30, 30, 40, 20, 0.9f, 100, 50, 20, 20, 0.8f}
                               : std::vector<float>{10, 20, 50, 40, 0.9f, 0, 90, 40, 110, 60, 0.8f, 0}};
        REQUIRE((decoder.Forward(single) == COSMO_NN_OK));
        REQUIRE((decoder.Forward(batch) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {30, 30, 40, 20, 0.9f, 0});
        decoder.CheckRow(1, 0, {100, 50, 20, 20, 0.8f, 0});
        decoder.CheckRow(1, 1, {0, 0, 0, 0, 0, 0});
        REQUIRE((decoder.Forward(single) == COSMO_NN_OK));
        CHECK((decoder.output.blob->GetBlobDesc().dims == DimsVector{2, 4, 6}));
        CHECK(std::all_of(decoder.output.values.begin() + 6, decoder.output.values.end(),
                          [](float value) { return value == 0.f; }));
    }

    TEST_CASE("YOLO end-to-end raw node enforces top-k after class-aware suppression",
              "[nn][yolo26][decode]") {
        DecodeTensor input{
            {1, 5, 3}, RawRows({{50, 50, 40, 40, 0.9f}, {51, 50, 40, 40, 0.8f}, {150, 50, 20, 20, 0.7f}})};
        E2EDecoder decoder(true, 2);
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        decoder.CheckRow(0, 0, {50, 50, 40, 40, 0.9f, 0});
        decoder.CheckRow(0, 1, {150, 50, 20, 20, 0.7f, 0});
        decoder.CheckRow(1, 0, {0, 0, 0, 0, 0, 0});
    }

    TEST_CASE("YOLO end-to-end malformed shapes fail before accessing buffers", "[nn][yolo26][decode]") {
        const bool raw = GENERATE(false, true);
        E2EDecoder decoder(raw);
        std::vector<DimsVector> shapes{{6},
                                       {1, 6},
                                       {1, 1, 6, 1},
                                       {0, 6, 6},
                                       {1, 0, 6},
                                       {1, 6, 0},
                                       {-1, 6, 6},
                                       {1, -1, 6},
                                       {1, 6, -1},
                                       {3, 6, 6},
                                       {2, std::numeric_limits<int>::max(), 6}};
        shapes.push_back(raw ? DimsVector{1, 4, 1} : DimsVector{1, 6, 1});
        shapes.push_back(raw ? DimsVector{1, 3, 6} : DimsVector{1, 1, 7});
        for (const auto& shape : shapes) {
            DecodeTensor input{shape, std::vector<float>(48)};
            CHECK((decoder.Forward(input) != COSMO_NN_OK));
        }
    }

    TEST_CASE("YOLO end-to-end rejects missing, extra and null tensors", "[nn][yolo26][decode]") {
        E2EDecoder decoder;
        DecodeTensor input{{1, 1, 6}, {10, 20, 50, 40, 0.9f, 0}};
        std::vector<std::shared_ptr<Blob>> bottoms{input.blob};
        std::vector<std::shared_ptr<Blob>> tops{decoder.output.blob};
        std::vector<std::shared_ptr<Blob>> empty;
        std::vector<std::shared_ptr<Blob>> extra{input.blob, input.blob};
        std::vector<std::shared_ptr<Blob>> nulls{nullptr};
        CHECK((decoder.node.Forward(empty, tops) != COSMO_NN_OK));
        CHECK((decoder.node.Forward(bottoms, empty) != COSMO_NN_OK));
        CHECK((decoder.node.Forward(extra, tops) != COSMO_NN_OK));
        CHECK((decoder.node.Forward(bottoms, extra) != COSMO_NN_OK));
        CHECK((decoder.node.Forward(nulls, tops) != COSMO_NN_OK));
        CHECK((decoder.node.Forward(bottoms, nulls) != COSMO_NN_OK));
        input.blob->SetHandle({});
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
    }

    TEST_CASE("YOLO end-to-end requires float tensors and sufficient declared output capacity",
              "[nn][yolo26][decode]") {
        E2EDecoder decoder;
        DecodeTensor input{{2, 1, 6}, {10, 20, 50, 40, 0.9f, 0, 10, 20, 50, 40, 0.8f, 1}};
        input.blob->GetBlobDesc().device_type = DEVICE_SOPHON_TPU;
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
        input.blob->GetBlobDesc().device_type          = DEVICE_NAIVE;
        decoder.output.blob->GetBlobDesc().device_type = DEVICE_SOPHON_TPU;
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
        decoder.output.blob->GetBlobDesc().device_type = DEVICE_NAIVE;
        input.blob->GetBlobDesc().data_type            = DATA_TYPE_INT32;
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
        input.blob->GetBlobDesc().data_type          = DATA_TYPE_FLOAT;
        decoder.output.blob->GetBlobDesc().data_type = DATA_TYPE_HALF;
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
        decoder.output.blob->GetBlobDesc().data_type = DATA_TYPE_FLOAT;
        for (const auto& shape : std::vector<DimsVector>{{24},
                                                         {2, 24},
                                                         {2, 4, 6, 1},
                                                         {1, 4, 6},
                                                         {2, 3, 6},
                                                         {2, 5, 6},
                                                         {2, 4, 5},
                                                         {0, 4, 6},
                                                         {2, -1, 6},
                                                         {2, std::numeric_limits<int>::max(), 6}}) {
            decoder.output.blob->GetBlobDesc().dims = shape;
            CHECK((decoder.Forward(input) != COSMO_NN_OK));
        }
        decoder.output.blob->GetBlobDesc().dims = {2, 4, 6};
        decoder.output.blob->SetHandle({});
        CHECK((decoder.Forward(input) != COSMO_NN_OK));
    }

    TEST_CASE("YOLO end-to-end shape inference rejects invalid parameters", "[nn][yolo26][decode]") {
        for (int top_k : {0, -1, std::numeric_limits<int>::max()}) {
            YoloE2EDecodeNode node;
            auto op = pipeline_utils::MakeYoloE2EPostOp(0.25f, top_k, 200, 100, 0.7f, true);
            node.SetMaxBatch(2);
            node.LoadParam(op.get());
            CHECK((node.InferTopShapes() != COSMO_NN_OK));
        }
        for (float invalid :
             {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            for (bool invalid_conf : {false, true}) {
                YoloE2EDecodeNode node;
                auto op = pipeline_utils::MakeYoloE2EPostOp(invalid_conf ? invalid : 0.25f, 4, 200, 100,
                                                            invalid_conf ? 0.7f : invalid, true);
                node.SetMaxBatch(2);
                node.LoadParam(op.get());
                CHECK((node.InferTopShapes() != COSMO_NN_OK));
            }
        }
        YoloE2EDecodeNode node;
        node.LoadParam(nullptr);
        CHECK((node.InferTopShapes() != COSMO_NN_OK));
        auto op = pipeline_utils::MakeYoloE2EPostOp(0.25f, 4, 200, 100, 1.1f, true);
        node.SetMaxBatch(2);
        node.LoadParam(op.get());
        CHECK((node.InferTopShapes() != COSMO_NN_OK));
        op = pipeline_utils::MakeYoloE2EPostOp(0.25f, 4, 200, 100, 0.7f, true);
        node.LoadParam(op.get());
        node.SetMaxBatch(0);
        CHECK((node.InferTopShapes() != COSMO_NN_OK));
    }

}  // namespace
}  // namespace cosmo::nn
