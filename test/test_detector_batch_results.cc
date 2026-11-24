#include <array>

#include "catch_amalgamated.hpp"
#include "infer/AiCommon.h"
#include "infer/DetectorBatchResults.h"
#include "nn/utils/net_utils.h"

namespace cosmo {

TEST_CASE("Detector batches reject missing, partial and excess frame results",
          "[infer][detector][privacy][batch]") {
    std::vector<std::vector<AiDetectRstEl>> results(1);
    AiDetectRstEl existing;
    existing.confidence.label = "prior-frame";
    results[0].push_back(existing);
    std::vector<std::vector<AiDetectRstEl>> batch;
    SECTION("empty outer result is not a zero-target detection") {}
    SECTION("a partial batch cannot be associated with all inputs") {
        batch.resize(1);
    }
    SECTION("an oversized batch cannot be silently truncated") {
        batch.resize(3);
    }

    CHECK_FALSE(infer_detail::AppendCompleteDetectorBatch(2, std::move(batch), results));
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].size() == 1);
    CHECK(results[0][0].confidence.label == "prior-frame");
}

TEST_CASE("Detector batches preserve explicit empty frames and input order",
          "[infer][detector][privacy][batch]") {
    std::vector<std::vector<AiDetectRstEl>> results;
    std::vector<std::vector<AiDetectRstEl>> zeroBatch(2);
    REQUIRE(infer_detail::AppendCompleteDetectorBatch(2, std::move(zeroBatch), results));
    REQUIRE(results.size() == 2);
    CHECK(results[0].empty());
    CHECK(results[1].empty());

    std::vector<std::vector<AiDetectRstEl>> mixedBatch(2);
    AiDetectRstEl target;
    target.confidence.label = "person";
    target.box              = {80, 20, 30, 70};
    mixedBatch[1].push_back(target);
    REQUIRE(infer_detail::AppendCompleteDetectorBatch(2, std::move(mixedBatch), results));
    REQUIRE(results.size() == 4);
    CHECK(results[2].empty());
    REQUIRE(results[3].size() == 1);
    CHECK(results[3][0].confidence.label == "person");
    CHECK(results[3][0].box.x == 80);
}

TEST_CASE("Detection parser trims static padded batches before strict result validation",
          "[infer][detector][privacy][batch][padding]") {
    // A static four-image tensor with only one actual input. The three unused slots
    // deliberately contain confident boxes: none may escape the parser as another frame.
    std::vector<float> tensor = {100, 100, 20, 40, 0.9F, 0, 200, 100, 20, 40, 0.9F, 0,
                                 300, 100, 20, 40, 0.9F, 0, 400, 100, 20, 40, 0.9F, 0};
    bool hasTarget            = true;
    SECTION("real frame contains a target") {}
    SECTION("real frame is a legitimate zero-target result") {
        tensor[4] = 0.1F;
        hasTarget = false;
    }
    nn::BlobDesc desc;
    desc.dims = {4, 1, 6};
    nn::BlobHandle handle;
    handle.base = tensor.data();
    auto blob   = std::make_shared<nn::Blob>(desc, handle);
    std::vector<std::vector<nn::ObjectInfoV1>> parsed;
    REQUIRE(static_cast<bool>(nn::NetUtils::PickDetectionObjects(
        blob, {nn::Size(640, 640)}, nn::Size(640, 640), {0}, {0.25F}, {"person"}, parsed)));
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0].size() == (hasTarget ? 1 : 0));

    std::vector<std::vector<nn::ObjectInfoV1>> complete;
    REQUIRE(infer_detail::AppendCompleteDetectorBatch(1, std::move(parsed), complete));
    REQUIRE(complete.size() == 1);
    CHECK(complete[0].size() == (hasTarget ? 1 : 0));
}

TEST_CASE("Rockchip detection boxes undo the actual resize geometry", "[infer][detector][resize]") {
    const int gravity = GENERATE(0, 1, 2);
    // A 320x181 source letterboxed to 320x224 has 43 extra rows. The
    // centered mode puts 21 above the image and 22 below it.
    const float model_y = gravity == 0 ? 50.f * 224 / 181 : (gravity == 1 ? 71.f : 50.f);
    const float model_h = gravity == 0 ? 20.f * 224 / 181 : 20.f;
    std::array<float, 6> values{60.f, model_y, 20.f, model_h, .9f, 0.f};
    nn::BlobDesc desc;
    desc.dims = {1, 1, 6};
    nn::BlobHandle handle;
    handle.base = values.data();
    auto blob   = std::make_shared<nn::Blob>(desc, handle);
    nn::Resize resize;
    resize.dsize   = {224, 320};
    resize.gravity = gravity;
    std::vector<std::vector<nn::ObjectInfoV1>> parsed;
    REQUIRE(bool(
        nn::NetUtils::ParseDetectionOutput({blob}, {{320, 181}}, {320, 224}, {}, {}, {}, parsed, &resize)));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed.front().size() == 1);
    const auto& box = parsed.front().front();
    CHECK(box.x1 == Catch::Approx(50.f));
    CHECK(box.x2 == Catch::Approx(70.f));
    CHECK(box.y1 == Catch::Approx(40.f));
    CHECK(box.y2 == Catch::Approx(60.f));
}

}  // namespace cosmo
