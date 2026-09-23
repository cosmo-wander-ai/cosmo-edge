#include "catch_amalgamated.hpp"
#include "service/management/ManagedModelConfig.h"
using cosmo::service::MergeManagedModelConfig;
using nlohmann::json;
namespace {
json NativeModel() {
    return {{"algorithm_code", "5000012"},
            {"chip_type", "BM1688"},
            {"model_type", "yolov8_det"},
            {"models",
             {{{"file_name", "model.nn"},
               {"file_md5", "binary-fingerprint"},
               {"inputs", {{{"name", "images"}, {"shape", {1, 3, 640, 640}}}}},
               {"outputs", {{{"name", "output"}, {"shape", {1, 84, 8400}}}}},
               {"params", {{"confidence_threshold", 0.25}}}}}},
            {"labels", json::array()}};
}
}  // namespace
TEST_CASE("Managed model configuration keeps native identity and file metadata", "[management]") {
    const auto native                                   = NativeModel();
    auto edit                                           = native;
    edit["algorithm_code"]                              = "platform-resource";
    edit["models"][0]["file_name"]                      = "../../other-model";
    edit["models"][0]["params"]["confidence_threshold"] = 0.6;
    edit["labels"]    = {{{"id", "0"}, {"name", "person"}, {"threshold", {0.3, 0.4}}}};
    const auto merged = MergeManagedModelConfig(native, edit);
    REQUIRE(merged["algorithm_code"] == native["algorithm_code"]);
    REQUIRE(merged["models"][0]["file_name"] == "model.nn");
    REQUIRE(merged["models"][0]["file_md5"] == "binary-fingerprint");
    REQUIRE(merged["models"][0]["params"]["confidence_threshold"] == 0.6);
    REQUIRE(merged["labels"] == edit["labels"]);
}
TEST_CASE("Managed model rejects chip type tensor and label mismatches", "[management]") {
    const auto native = NativeModel();
    auto edit         = native;
    SECTION("chip") {
        edit["chip_type"] = "CV186X";
    }
    SECTION("type") {
        edit["model_type"] = "classify";
    }
    SECTION("shape") {
        edit["models"][0]["inputs"][0]["shape"] = {1, 3, 320, 320};
    }
    SECTION("labels") {
        edit["labels"] = {{{"id", "0"}, {"name", "person"}, {"threshold", {-1, 0.3}}}};
    }
    REQUIRE_THROWS_AS(MergeManagedModelConfig(native, edit), cosmo::service::ManagementError);
}
TEST_CASE("Managed generation parameters preserve other native configuration", "[management]") {
    auto native            = NativeModel();
    native["model_type"]   = "qwen3_5";
    native["config"]       = {{"path", "native/tokenizer.json"}, {"generation", {{"temperature", 0.3}}}};
    auto edit              = native;
    edit["config"]["path"] = "/untrusted";
    edit["config"]["generation"]["temperature"] = 0.5;
    const auto merged                           = MergeManagedModelConfig(native, edit);
    REQUIRE(merged["config"]["path"] == "native/tokenizer.json");
    REQUIRE(merged["config"]["generation"]["temperature"] == 0.5);
}
