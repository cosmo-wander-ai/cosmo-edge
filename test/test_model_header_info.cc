#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "infer/BmodelTool.h"
#include "nn/utils/model_header_info.h"

namespace fs = std::filesystem;

namespace {

std::vector<char> ReadBinaryFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return std::vector<char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

fs::path MakeTempDir(const std::string& name) {
    auto suffix  = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() / (name + "_" + std::to_string(suffix));
    fs::create_directories(dir);
    return dir;
}

void WriteBinaryFile(const fs::path& path, const std::string& data) {
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.good());
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    REQUIRE(output.good());
}

}  // namespace

TEST_CASE("plain nn header serializes parses and validates", "[model_header]") {
    auto build_result = cosmo::nn::BuildPlainNnHeader({11, 22});
    REQUIRE(build_result);

    auto encoded = build_result.data;

    auto parse_result = cosmo::nn::ParsePlainNnHeader(encoded);
    REQUIRE(parse_result);
    REQUIRE(parse_result.header.model_count == 2);
    REQUIRE(parse_result.header.model_sizes[0] == 11);
    REQUIRE(parse_result.header.model_sizes[1] == 22);
    REQUIRE(cosmo::nn::ValidatePlainNnFileSize(parse_result.header,
                                               static_cast<uint64_t>(cosmo::nn::kPlainNnHeaderSize + 33)));

    auto invalid = encoded;
    invalid[0]   = 'X';
    REQUIRE_FALSE(cosmo::nn::ParsePlainNnHeader(invalid));

    invalid    = encoded;
    invalid[4] = 2;
    REQUIRE_FALSE(cosmo::nn::ParsePlainNnHeader(invalid));

    invalid    = encoded;
    invalid[6] = static_cast<char>(cosmo::nn::kPlainNnHeaderSize - 1);
    REQUIRE_FALSE(cosmo::nn::ParsePlainNnHeader(invalid));

    invalid     = encoded;
    invalid[8]  = 0;
    invalid[9]  = 0;
    invalid[10] = 0;
    invalid[11] = 0;
    REQUIRE_FALSE(cosmo::nn::ParsePlainNnHeader(invalid));

    invalid     = encoded;
    invalid[12] = 1;
    REQUIRE_FALSE(cosmo::nn::ParsePlainNnHeader(invalid));

    invalid     = encoded;
    invalid[32] = 1;
    REQUIRE_FALSE(cosmo::nn::ParsePlainNnHeader(invalid));

    REQUIRE_FALSE(cosmo::nn::BuildPlainNnHeader({0}));

    std::vector<uint64_t> too_many(cosmo::nn::kPlainNnMaxModelCount + 1, 1);
    REQUIRE_FALSE(cosmo::nn::BuildPlainNnHeader(too_many));

    build_result = cosmo::nn::BuildPlainNnHeader({11});
    REQUIRE(build_result);
    parse_result = cosmo::nn::ParsePlainNnHeader(build_result.data);
    REQUIRE(parse_result);
    REQUIRE_FALSE(
        cosmo::nn::ValidatePlainNnFileSize(parse_result.header, cosmo::nn::kPlainNnHeaderSize + 12));
}

TEST_CASE("plain nn header allows segments larger than 512MB", "[model_header]") {
    constexpr uint64_t large_segment_size = 512ULL * 1024ULL * 1024ULL + 1ULL;

    auto build_result = cosmo::nn::BuildPlainNnHeader({large_segment_size});
    REQUIRE(build_result);

    auto parse_result = cosmo::nn::ParsePlainNnHeader(build_result.data);
    REQUIRE(parse_result);
    REQUIRE(parse_result.header.model_sizes[0] == large_segment_size);
    REQUIRE(cosmo::nn::ValidatePlainNnFileSize(
        parse_result.header, static_cast<uint64_t>(cosmo::nn::kPlainNnHeaderSize) + large_segment_size));
}

TEST_CASE("BmodelTool ConvertToNn writes plain header and concatenated payloads", "[model_header]") {
    fs::path dir          = MakeTempDir("cosmo_model_header");
    fs::path first_model  = dir / "first.bmodel";
    fs::path second_model = dir / "second.bmodel";
    fs::path output_model = dir / "model.nn";

    WriteBinaryFile(first_model, "abc");
    WriteBinaryFile(second_model, "defgh");

    std::string error =
        cosmo::BmodelTool::ConvertToNn({first_model.string(), second_model.string()}, output_model.string());
    REQUIRE(error.empty());

    std::vector<char> data = ReadBinaryFile(output_model);
    REQUIRE(data.size() == cosmo::nn::kPlainNnHeaderSize + 8);

    std::array<char, cosmo::nn::kPlainNnHeaderSize> encoded{};
    std::copy(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(encoded.size()), encoded.begin());

    auto parse_result = cosmo::nn::ParsePlainNnHeader(encoded);
    REQUIRE(parse_result);
    REQUIRE(parse_result.header.model_count == 2);
    REQUIRE(parse_result.header.model_sizes[0] == 3);
    REQUIRE(parse_result.header.model_sizes[1] == 5);

    std::string payload(data.begin() + static_cast<std::ptrdiff_t>(cosmo::nn::kPlainNnHeaderSize),
                        data.end());
    REQUIRE(payload == "abcdefgh");

    fs::remove_all(dir);
}

TEST_CASE("BmodelTool ConvertToNn refuses output that aliases an input", "[model_header]") {
    fs::path dir   = MakeTempDir("cosmo_model_header_alias");
    fs::path model = dir / "model.nn";

    WriteBinaryFile(model, "abc");

    std::string error = cosmo::BmodelTool::ConvertToNn({model.string()}, model.string());
    REQUIRE_FALSE(error.empty());

    std::vector<char> data = ReadBinaryFile(model);
    REQUIRE(std::string(data.begin(), data.end()) == "abc");

    fs::remove_all(dir);
}
