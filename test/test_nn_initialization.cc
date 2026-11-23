#include <memory>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/core/shared_resource.h"
#include "nn/node/sam_prompt_encode_node.h"
#include "nn/node/yolo_decode_node.h"
#include "nn/node/yolov8_decode_node.h"
#include "nn/utils/op.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "nn/device/sophon/sophon_affine_crop_node.h"
#include "nn/device/sophon/sophon_dino_encode_node.h"
#include "nn/device/sophon/sophon_sequence_node.h"
#else
#include "nn/device/cpu/cpu_affine_crop_node.h"
#include "nn/device/cpu/cpu_dino_encode_node.h"
#include "nn/device/cpu/cpu_sequence_node.h"
#endif

namespace cosmo::nn {
namespace {

#ifdef COSMO_NN_USE_SOPHON_BACKEND
    using AffineNode   = SophonAffineCropNode;
    using DinoNode     = SophonDinoEncodeNode;
    using SequenceNode = SophonSequenceNode;
#else
    using AffineNode   = CpuAffineCropNode;
    using DinoNode     = CpuDinoEncodeNode;
    using SequenceNode = CpuSequenceNode;
#endif

    TEST_CASE("NN YOLO decoder defaults form bounded outputs and accept overrides", "[nn][initialization]") {
        const bool yolov8 = GENERATE(false, true);
        std::unique_ptr<Node> node;
        if (yolov8)
            node = std::make_unique<YoloV8DecodeNode>();
        else
            node = std::make_unique<YoloDecodeNode>();

        REQUIRE(bool(node->InferTopShapes()));
        CHECK(node->GetTopBlobShapes() == std::vector<DimsVector>{{1, 1000, 6}});

        YoloPost op;
        op.top_k = 7;
        node->SetMaxBatch(2);
        node->LoadParam(&op);
        REQUIRE(bool(node->InferTopShapes()));
        CHECK(node->GetTopBlobShapes() == std::vector<DimsVector>{{2, 7, 6}});
    }

    TEST_CASE("YOLOv8 enables RKNN direct candidates only for square inputs",
              "[nn][rknn][yolov8][direct-candidates]") {
        const auto [width, height, use_direct] = GENERATE(table<int, int, bool>({
            {640, 640, true},
            {640, 320, false},
            {320, 640, false},
        }));
        CAPTURE(width, height);

        SharedResource shared;
        YoloV8DecodeNode node;
        node.SetSharedResource(&shared);
        YoloPost op;
        op.input_width  = width;
        op.input_height = height;
        node.LoadParam(&op);

        REQUIRE(bool(node.InferTopShapes()));
        CHECK(shared.yolov8_direct_postprocess.configured == use_direct);
    }

    TEST_CASE("NN affine crop rejects missing dimensions before using shared resources",
              "[nn][initialization]") {
        AffineNode node;
        CHECK_FALSE(bool(node.InferTopShapes()));
        AffineCrop op;
        REQUIRE_NOTHROW(node.LoadParam(&op));
        CHECK_FALSE(bool(node.InferTopShapes()));

        SharedResource shared;
        node.SetSharedResource(&shared);
        op.output_hw = {16, 32};
        node.LoadParam(&op);
        REQUIRE(bool(node.InferTopShapes()));
        CHECK(shared.net_input_w == 32);
        CHECK(shared.net_input_h == 16);
#ifdef COSMO_NN_USE_SOPHON_BACKEND
        CHECK(node.GetTopBlobShapes() == std::vector<DimsVector>{{1, 16, 64, 3}});
#else
        CHECK(node.GetTopBlobShapes() == std::vector<DimsVector>{{1, 16, 32, 3}});
#endif

        op.output_hw.clear();
        REQUIRE_NOTHROW(node.LoadParam(&op));
        CHECK_FALSE(bool(node.InferTopShapes()));
    }

    TEST_CASE("NN sequence requires its dimensions and sequence length", "[nn][initialization]") {
        SequenceNode node;
        CHECK_FALSE(bool(node.InferTopShapes()));
        Sequence op;
        REQUIRE_NOTHROW(node.LoadParam(&op));
        CHECK_FALSE(bool(node.InferTopShapes()));

        op.dsize = {8, 12};
        node.LoadParam(&op);
        CHECK_FALSE(bool(node.InferTopShapes()));

        op.size = 2;
        node.LoadParam(&op);
        REQUIRE(bool(node.InferTopShapes()));
        CHECK(node.GetTopBlobShapes() == std::vector<DimsVector>{{1, 3, 8, 12}});
        node.LoadParam(nullptr);
        CHECK_FALSE(bool(node.InferTopShapes()));
    }

    TEST_CASE("NN DINO dimensions and SAM point counts are defined before execution",
              "[nn][initialization]") {
        DinoNode dino;
        CHECK_FALSE(bool(dino.InferTopShapes()));
        DinoEncoder encoder;
        dino.LoadParam(&encoder);
        CHECK_FALSE(bool(dino.InferTopShapes()));
        encoder.dst_width  = 800;
        encoder.dst_height = 800;
        dino.LoadParam(&encoder);
        REQUIRE(bool(dino.InferTopShapes()));
        REQUIRE(dino.GetTopBlobShapes().size() == 8);
        CHECK(dino.GetTopBlobShapes().front() == DimsVector{1, 3, 800, 800});

        SAMPromptEncodeNode prompt;
        REQUIRE(bool(prompt.InferTopShapes()));
        CHECK(prompt.GetTopBlobShapes() == std::vector<DimsVector>{{1, 6, 2}});
        SAMPromptEncode op;
        op.max_points = 4;
        prompt.LoadParam(&op);
        REQUIRE(bool(prompt.InferTopShapesWithBottoms({{2, 3, 2}}, {DATA_TYPE_FLOAT})));
        CHECK(prompt.GetTopBlobShapes() == std::vector<DimsVector>{{2, 4, 2}});
    }

}  // namespace
}  // namespace cosmo::nn
