#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numeric>

#include "catch_amalgamated.hpp"
#include "nn/pipeline/model_pipeline.h"
#include "nn/utils/laya_frontend.h"
#include "tokenizers_cpp.h"

using namespace cosmo::nn::laya;

TEST_CASE("Native Laya preserves ordered questions and rejects overflow", "[laya-native]") {
    nlohmann::ordered_json q = {
        {"type", "choice"},
        {"instructions", "helmet"},
        {"criteria", nlohmann::ordered_json::array({{{"label", "wearing"}, {"description", "yes"}},
                                                    {{"label", "missing"}, {"description", "no"}}})}};
    nlohmann::json config = {{"temperature", {1.f, 1.f, 1.f}}};
    auto encode           = [](const std::string& text) { return std::vector<int32_t>(text.size(), 10); };
    auto result           = CompileQuestion(q, config, encode);
    REQUIRE(result.labels == std::vector<std::string>{"wearing", "missing"});
    REQUIRE(result.tokens.front() == 2);
    REQUIRE(result.tokens.back() == 1);
    REQUIRE(result.tokens.at(result.markers.at(0)) == 4);
    REQUIRE(result.tokens.at(result.markers.at(1)) == 4);
    REQUIRE(result.tokens.size() == result.image_position + kImageSlots + 1);
    q["instructions"] = std::string(200, 'a');
    REQUIRE_THROWS_WITH(CompileQuestion(q, config, encode),
                        Catch::Matchers::ContainsSubstring("sequence budget exceeded"));
}

TEST_CASE("Native Laya mask never exposes padding keys", "[laya-native]") {
    std::vector<float> full, local;
    AttentionMasks(170, full, local);
    for (int row : {0, 64, 169, 170, 233, 234, 255}) {
        int visible = 0;
        for (int column = 0; column < kSequence; ++column) {
            if (column >= 170) {
                REQUIRE(full[row * kSequence + column] == -10000.f);
                REQUIRE(local[row * kSequence + column] == -10000.f);
            }
            visible += local[row * kSequence + column] == 0.f;
        }
        REQUIRE(visible > 0);
    }
    REQUIRE(local[0] == 0.f);
    REQUIRE(local[65] == -10000.f);
    REQUIRE(local[255 * kSequence] == 0.f);
}

TEST_CASE("Native Laya pipeline is selected by model type", "[laya-native]") {
    auto& registry = cosmo::nn::ModelPipelineRegistry::Instance();
    REQUIRE(registry.Has("laya_v"));
    std::unique_ptr<cosmo::nn::ModelPipeline> pipeline(registry.Create("laya_v"));
    REQUIRE(pipeline->GetModelType() == "laya_v");
    REQUIRE(pipeline->GetOutputCategory() == cosmo::nn::OutputCategory::TEXT);
}

TEST_CASE("Native Laya matches frozen Python CPU boundaries", "[laya-native-reference]") {
    const char* directory = std::getenv("COSMO_LAYA_REFERENCE_DIR");
    if (!directory)
        SKIP("Requires generated numeric reference assets");
    const auto root = std::filesystem::path(directory);
    auto read_json  = [&](const std::filesystem::path& path) {
        std::ifstream in(path);
        return nlohmann::ordered_json::parse(in);
    };
    auto manifest         = read_json(root / "reference.json");
    auto tokenizer_config = read_json(root / "tokenizer.json");
    auto tokenizer        = tokenizers::Tokenizer::FromBlobJSON(TokenizerJson(tokenizer_config));
    auto config           = read_json(root / "laya_config.json");
    auto heads            = ReadHeads((root / "heads").string());
    EmbeddingTable table((root / "token_embeddings.f16.npy").string());
    for (const auto& item : manifest.at("questions")) {
        auto q = CompileQuestion(item.at("question"), config,
                                 [&](const auto& text) { return tokenizer->Encode(text); });
        REQUIRE(q.tokens == item.at("tokens").get<std::vector<int32_t>>());
        REQUIRE(q.markers == item.at("markers").get<std::vector<int>>());
        REQUIRE(q.labels == item.at("labels").get<std::vector<std::string>>());
        REQUIRE(q.image_position == item.at("image_position").get<size_t>());
        auto prefix                  = item.at("prefix").get<std::string>();
        auto visual                  = ReadFloatNpy((root / "visual.npy").string());
        auto embedded                = table.Splice(q, visual.data);
        const auto expected_embedded = ReadFloatNpy((root / (prefix + "-embeds.npy")).string()).data;
        REQUIRE(
            std::equal(embedded.begin(), embedded.end(), expected_embedded.begin(), expected_embedded.end()));
        std::vector<float> full, sliding;
        AttentionMasks(q.tokens.size(), full, sliding);
        REQUIRE(full == ReadFloatNpy((root / (prefix + "-full.npy")).string()).data);
        REQUIRE(sliding == ReadFloatNpy((root / (prefix + "-sliding.npy")).string()).data);
        auto hidden       = ReadFloatNpy((root / "hidden.npy").string());
        auto score        = Score(hidden.data, q, heads);
        const auto logits = item.at("logits").get<std::vector<float>>();
        const auto acts   = item.at("action_logits").get<std::vector<float>>();
        for (size_t i = 0; i < logits.size(); ++i)
            REQUIRE(score.logits[i] == Catch::Approx(logits[i]).margin(0.0001));
        for (size_t i = 0; i < acts.size(); ++i)
            REQUIRE(score.action_logits[i] == Catch::Approx(acts[i]).margin(0.0001));
    }
    for (const auto& image : manifest.at("images")) {
        int width = image.at("width"), height = image.at("height");
        auto name = image.at("name").get<std::string>();
        std::ifstream in(root / (name + ".bgr"), std::ios::binary);
        std::vector<uint8_t> bytes(size_t(width) * height * 3);
        in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        REQUIRE(in.good());
        auto pixels   = ImagePatches(bytes.data(), width, height, width * 3);
        auto expected = ReadFloatNpy((root / (name + "-pixels.npy")).string());
        REQUIRE(std::equal(pixels.begin(), pixels.end(), expected.data.begin(), expected.data.end()));
    }
}
