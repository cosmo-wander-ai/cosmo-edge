#include <nlohmann/json.hpp>

#include "catch_amalgamated.hpp"
#include "service/model/IModelQuery.h"

TEST_CASE("Model query exposes complete metadata and stable JSON", "[model-dto]") {
    cosmo::ModelInfo model;
    REQUIRE(model.timestamp == 0);
    cosmo::ModelLabel label;
    REQUIRE(label.confidenceHigh == -1.0f);
    REQUIRE(label.confidence == -1.0f);
    const nlohmann::json empty = model;
    REQUIRE(empty == nlohmann::json::parse(R"({"id":"","name":"","version":"","timestamp":0,"labels":[]})"));

    model.id             = "m1";
    model.name           = "中文\"模型\\\n";
    model.version        = "v2";
    model.timestamp      = 5000000000LL;
    model.path           = "/private/model";
    label.code           = "person";
    label.labelName      = "not serialized";
    label.label          = "also not serialized";
    label.confidenceHigh = 0.75f;
    label.confidence     = 0.5f;
    model.labels.push_back(label);
    const nlohmann::json expected = {
        {"id", "m1"},
        {"name", "中文\"模型\\\n"},
        {"version", "v2"},
        {"timestamp", 5000000000LL},
        {"labels", {{{"code", "person"}, {"confidenceHigh", 0.75}, {"confidence", 0.5}}}}};
    const nlohmann::json encoded = model;
    REQUIRE(encoded == expected);
    const auto decoded = nlohmann::json::parse(encoded.dump(-1, ' ', true)).get<cosmo::ModelInfo>();
    REQUIRE(decoded.name == model.name);
    REQUIRE(decoded.timestamp == model.timestamp);
    REQUIRE(decoded.path.empty());
    REQUIRE(decoded.labels.at(0).labelName.empty());
    REQUIRE(decoded.labels.at(0).label.empty());
}

TEST_CASE("Model metadata keeps absent and null fields and rejects wrong types", "[model-dto]") {
    cosmo::ModelLabel label{"code", "name", "label", 0.75f, 0.5f};
    cosmo::ModelInfo model{"id", "name", "version", 42, "path", {label}};
    nlohmann::json::object().get_to(model);
    nlohmann::json::parse(
        R"({"id":null,"name":null,"version":null,"timestamp":null,"labels":null,"path":"ignored"})")
        .get_to(model);
    REQUIRE(model.id == "id");
    REQUIRE(model.name == "name");
    REQUIRE(model.version == "version");
    REQUIRE(model.timestamp == 42);
    REQUIRE(model.path == "path");
    REQUIRE(model.labels.size() == 1);
    nlohmann::json::object().get_to(label);
    nlohmann::json::parse(
        R"({"code":null,"confidenceHigh":null,"confidence":null,"labelName":"ignored","label":"ignored"})")
        .get_to(label);
    REQUIRE(label.code == "code");
    REQUIRE(label.labelName == "name");
    REQUIRE(label.label == "label");
    REQUIRE(label.confidenceHigh == 0.75f);
    REQUIRE(label.confidence == 0.5f);
    REQUIRE_THROWS_AS(nlohmann::json({{"timestamp", "wrong"}}).get_to(model), nlohmann::json::type_error);
    REQUIRE_THROWS_AS(nlohmann::json({{"confidence", "wrong"}}).get_to(label), nlohmann::json::type_error);
    nlohmann::json({{"labels", nlohmann::json::array()}}).get_to(model);
    REQUIRE(model.labels.empty());
}
