#include <array>
#include <limits>
#include <memory>
#include <vector>

#include "catch_amalgamated.hpp"
#include "nn/node/node_type_utils.h"
#include "nn/node/yolo_e2e_decode_node.h"
#include "nn/pipeline/detection_pipeline.h"
#include "nn/pipeline/pipeline_utils.h"
#include "nn/utils/net_utils.h"
#include "nn/utils/op.h"
#include "nn/utils/person_attribute_layout.h"

namespace cosmo::nn {
namespace {
    struct Tensor {
        std::vector<float> data;
        std::shared_ptr<Blob> blob;
        Tensor(DimsVector dims, std::vector<float> values) : data(std::move(values)) {
            BlobDesc desc;
            desc.dims = std::move(dims);
            BlobHandle handle;
            handle.base = data.data();
            blob        = std::make_shared<Blob>(desc, handle);
        }
    };

    struct PersonDecoder {
        YoloE2EDecodeNode node;
        Tensor output;
        explicit PersonDecoder(float confidence = 0.25f, int row_width = 10)
            : output({2, 4, row_width}, std::vector<float>(8 * row_width, -99.f)) {
            YoloPost op("yolo26_person_attributes_postprocess");
            op.nms_detection_conf       = confidence;
            op.top_k                    = 4;
            op.input_width              = 200;
            op.input_height             = 100;
            op.person_attribute_columns = row_width;
            node.SetMaxBatch(2);
            node.LoadParam(&op);
            REQUIRE((node.InferTopShapes() == COSMO_NN_OK));
        }
        Status Forward(Tensor& input) {
            std::vector<std::shared_ptr<Blob>> bottom{input.blob};
            std::vector<std::shared_ptr<Blob>> top{output.blob};
            return node.Forward(bottom, top);
        }
    };

    TEST_CASE("Person attributes decoded rows preserve all independent scores", "[nn][yolo26][attributes]") {
        CHECK(NodeTypeUtils::NodeTypeFromStr("yolo26_person_attributes_postprocess") == NODE_YOLO_E2E_DECODE);
        PersonDecoder decoder;
        Tensor input({1, 2, 10},
                     {0, 0, 20, 20, .25f, 0, 0, .1f, .2f, 1, 10, 20, 50, 40, .9f, 0, .9f, 0, .5f, .3f});
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        const std::array<float, 10> first{10, 10, 20, 20, .25f, 0, 0, .1f, .2f, 1};
        for (size_t i = 0; i < first.size(); ++i)
            CHECK(decoder.output.data[i] == Catch::Approx(first[i]));
        CHECK(decoder.output.data[10 + 6] == Catch::Approx(.9f));
        CHECK(decoder.output.data[10 + 7] == 0.f);
        for (size_t i = 20; i < decoder.output.data.size(); ++i)
            CHECK(decoder.output.data[i] == 0.f);
        CHECK((decoder.output.blob->GetBlobDesc().dims == DimsVector{2, 4, 10}));
    }

    TEST_CASE("Person attributes reject padding and invalid numbers without suppressing zero attributes",
              "[nn][yolo26][attributes]") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        PersonDecoder decoder(0.f);
        Tensor input({1, 9, 10}, {0,  0, 0,  0,  0,    0, 0, 0, 0, 0,     1, 2, 10,  12, .4f, 0, 0, 0, 0,   0,
                                  1,  2, 10, 12, .4f,  1, 0, 0, 0, 0,     1, 2, 10,  12, nan, 0, 0, 0, 0,   0,
                                  1,  2, 10, 12, 1.1f, 0, 0, 0, 0, 0,     1, 2, 10,  12, .4f, 0, 0, 0, nan, 0,
                                  1,  2, 10, 12, .4f,  0, 0, 0, 0, -0.1f, 1, 2, inf, 12, .4f, 0, 0, 0, 0,   0,
                                  10, 2, 1,  12, .4f,  0, 0, 0, 0, 0});
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        CHECK(decoder.output.data[4] == Catch::Approx(.4f));
        for (int i = 6; i < 10; ++i)
            CHECK(decoder.output.data[i] == 0.f);
        for (size_t i = 10; i < decoder.output.data.size(); ++i)
            CHECK(decoder.output.data[i] == 0.f);
        Tensor wrong({1, 2, 6}, std::vector<float>(12));
        CHECK_FALSE((decoder.Forward(wrong) == COSMO_NN_OK));
    }

    TEST_CASE("Person attribute parser maps one object and four independent scores",
              "[nn][yolo26][attributes]") {
        Tensor output({2, 2, 10},
                      {0,  5,  20, 20, .5f, 0, 0,   .25f, .75f, 1,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                       10, 10, 4,  4,  .9f, 0, .2f, .3f,  .4f,  .5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        std::vector<std::vector<ObjectInfoV1>> objects;
        const std::vector<std::shared_ptr<Blob>> blobs{output.blob};
        REQUIRE((NetUtils::ParsePersonAttributesOutput(blobs, {{200, 100}}, {200, 100}, .5f, "person",
                                                       objects) == COSMO_NN_OK));
        REQUIRE(objects.size() == 1);
        REQUIRE(objects[0].size() == 1);
        const auto& obj = objects[0][0];
        CHECK(obj.x1 == 0.f);
        CHECK(obj.y1 == 0.f);
        CHECK(obj.x2 == 10.f);
        CHECK(obj.y2 == 15.f);
        REQUIRE(obj.infos.size() == 5);
        CHECK(obj.infos[0].class_name == "person");
        CHECK(obj.infos[0].class_id == 0);
        CHECK(obj.infos[0].confidence == Catch::Approx(.5f));
        const std::array<const char*, 4> names{"smoking", "sleep", "fall", "normal"};
        const std::array<float, 4> scores{0, .25f, .75f, 1};
        for (int i = 0; i < 4; ++i) {
            CHECK(obj.infos[i + 1].class_name == names[i]);
            CHECK(obj.infos[i + 1].class_id == i);
            CHECK(obj.infos[i + 1].confidence == Catch::Approx(scores[i]));
        }
        REQUIRE((NetUtils::ParsePersonAttributesOutput(blobs, {{200, 100}, {200, 100}}, {200, 100}, .5f,
                                                       "person", objects) == COSMO_NN_OK));
        CHECK(objects.size() == 2);
        CHECK(objects[1].size() == 1);
    }

    TEST_CASE("Seven person attributes survive decoding and parsing in model order",
              "[nn][yolo26][attributes]") {
        PersonDecoder decoder(0.25f, 13);
        Tensor input({1, 2, 13}, {10, 20, 50, 40, .75f, 0, .1f, .2f, .3f, .4f, .5f, .6f, .7f,
                                  10, 5,  25, 15, .9f,  0, .1f, .2f, .3f, .4f, .5f, .6f, 1.1f});
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        CHECK((decoder.output.blob->GetBlobDesc().dims == DimsVector{2, 4, 13}));

        std::vector<std::vector<ObjectInfoV1>> objects;
        const std::vector<std::shared_ptr<Blob>> blobs{decoder.output.blob};
        REQUIRE((NetUtils::ParsePersonAttributesOutput(blobs, {{200, 100}}, {200, 100}, .25f, "person",
                                                       objects) == COSMO_NN_OK));
        REQUIRE(objects.size() == 1);
        REQUIRE(objects[0].size() == 1);
        const auto& object = objects[0][0];
        CHECK(object.x1 == 10.f);
        CHECK(object.y1 == 20.f);
        CHECK(object.x2 == 50.f);
        CHECK(object.y2 == 40.f);
        REQUIRE(object.infos.size() == 8);
        CHECK(object.infos[0].class_name == "person");
        CHECK(object.infos[0].confidence == Catch::Approx(.75f));
        const std::array<const char*, 7> names{"smoking",   "sleep",    "fall",  "phone_call",
                                               "phone_use", "climbing", "normal"};
        const std::array<float, 7> scores{.1f, .2f, .3f, .4f, .5f, .6f, .7f};
        for (size_t i = 0; i < names.size(); ++i) {
            CHECK(object.infos[i + 1].class_name == names[i]);
            CHECK(object.infos[i + 1].confidence == Catch::Approx(scores[i]));
        }

        Tensor legacy({1, 1, 10}, std::vector<float>(10));
        CHECK_FALSE((decoder.Forward(legacy) == COSMO_NN_OK));
        Tensor unknown({1, 1, 12}, std::vector<float>(12));
        CHECK_FALSE((NetUtils::ParsePersonAttributesOutput({unknown.blob}, {{200, 100}}, {200, 100}, .25f,
                                                           "person", objects) == COSMO_NN_OK));
    }

    TEST_CASE("Configured person attributes replace the legacy names at the same row width",
              "[nn][yolo26][attributes]") {
        PipelineConfig config;
        REQUIRE(
            (pipeline_utils::ParsePipelineConfig(
                 R"({"model_type":"yolo26_person_attributes","models":[{"outputs":[{"shape":[-1,-1,10]}]}],
                         "attributes":["smoking","sleep","fall","climbing"]})",
                 config) == COSMO_NN_OK));
        CHECK((config.person_attributes == std::vector<std::string>{"smoking", "sleep", "fall", "climbing"}));
        PersonDecoder decoder;
        Tensor input({1, 1, 10}, {10, 20, 50, 40, .75f, 0, .1f, .2f, .3f, .95f});
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        std::vector<std::vector<ObjectInfoV1>> objects;
        REQUIRE((NetUtils::ParsePersonAttributesOutput({decoder.output.blob}, {{200, 100}}, {200, 100}, .25f,
                                                       "person", objects,
                                                       config.person_attributes) == COSMO_NN_OK));
        REQUIRE(objects.size() == 1);
        REQUIRE(objects[0].size() == 1);
        REQUIRE(objects[0][0].infos.size() == 5);
        CHECK(objects[0][0].infos.back().class_name == "climbing");
        CHECK(objects[0][0].infos.back().confidence == Catch::Approx(.95f));
    }

    TEST_CASE("Configured person attribute outputs accept arbitrary named score columns",
              "[nn][yolo26][attributes]") {
        const nlohmann::json attributes{"helmet_missing", "PPE-gloves"};
        const auto names = ResolvePersonAttributeNames(8, &attributes);
        REQUIRE(names.has_value());
        PersonDecoder decoder(.25f, 8);
        Tensor input({1, 1, 8}, {10, 20, 50, 40, .75f, 0, .1f, .95f});
        REQUIRE((decoder.Forward(input) == COSMO_NN_OK));
        std::vector<std::vector<ObjectInfoV1>> objects;
        const std::vector<std::shared_ptr<Blob>> blobs{decoder.output.blob};
        CHECK_FALSE((NetUtils::ParsePersonAttributesOutput(blobs, {{200, 100}}, {200, 100}, .25f, "person",
                                                           objects) == COSMO_NN_OK));
        REQUIRE((NetUtils::ParsePersonAttributesOutput(blobs, {{200, 100}}, {200, 100}, .25f, "person",
                                                       objects, *names) == COSMO_NN_OK));
        REQUIRE(objects.size() == 1);
        REQUIRE(objects[0].size() == 1);
        REQUIRE(objects[0][0].infos.size() == 3);
        CHECK(objects[0][0].infos[1].class_name == "helmet_missing");
        CHECK(objects[0][0].infos[1].confidence == Catch::Approx(.1f));
        CHECK(objects[0][0].infos[2].class_name == "PPE-gloves");
        CHECK(objects[0][0].infos[2].confidence == Catch::Approx(.95f));

        for (const auto* bad : {"null", "[]", R"(["helmet_missing"])", R"(["sleep","sleep"])",
                                R"(["person","sleep"])", R"(["bad.name","sleep"])", R"([1,"sleep"])"}) {
            const auto raw = nlohmann::json::parse(bad);
            CHECK_FALSE(ResolvePersonAttributeNames(8, &raw).has_value());
        }
    }

    TEST_CASE("Person attribute model type rejects invalid input before model load",
              "[nn][yolo26][attributes]") {
        Yolo26PersonAttributesPipeline pipeline;
        PipelineConfig config;
        CHECK_FALSE((pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "") == COSMO_NN_OK));
        config.labels.push_back({"0", "person", {0.25f}});
        config.models.emplace_back();
        config.models[0].inputs.emplace_back();
        config.models[0].outputs.emplace_back();
        config.models[0].outputs[0].shape = {1, 300, 6};
        CHECK_FALSE((pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "") == COSMO_NN_OK));
        config.models[0].outputs[0].shape = {1, 300, 10};
        config.labels[0].name             = "smoking";
        CHECK_FALSE((pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "") == COSMO_NN_OK));
        config.labels[0].name = "person";
        config.labels[0].threshold.clear();
        CHECK_FALSE((pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "") == COSMO_NN_OK));
        config.labels[0].threshold   = {0.25f};
        config.models[0].params_json = R"({"raw_output":true})";
        CHECK_FALSE((pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "") == COSMO_NN_OK));
        config.models[0].params_json = R"({"top_k":0})";
        CHECK_FALSE((pipeline.Init(config, "", DEVICE_NAIVE, 0, nullptr, "", "") == COSMO_NN_OK));
    }
}  // namespace
}  // namespace cosmo::nn
