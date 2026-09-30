#include "catch_amalgamated.hpp"
#include "flow/recognizer/PRecognizer.h"
#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"

namespace {
class MissingPictureLandmarkModel final : public cosmo::service::IModelPathMapping {
public:
    std::vector<std::string> requests;
    void SetModelPathMapping(const std::string&, const std::string&) override {}
    std::string GetModelPathMapping(const std::string&) override {
        return {};
    }
    bool GetModelCfg(const std::string& code, std::string&, std::string&) override {
        requests.push_back(code);
        return false;
    }
    bool GetModelCfg(const std::string&, std::string&, std::string&, std::string&) override {
        return false;
    }
};
}  // namespace

TEST_CASE("Picture face embedding requires landmarks even in detector-only workflows", "[picture-face]") {
#if !defined(COSMO_MEDIA_USE_CPU_BACKEND)
    SKIP("CPU frame allocation required");
#else
    using namespace cosmo;
    mem::MemoryPoolMng pool(std::make_unique<mem::AllocatorCpu>(), {64 * 64 * 3});
    mem::SetMemoryPoolContext(&pool);
    struct ResetMemoryContext {
        ~ResetMemoryContext() {
            mem::SetMemoryPoolContext(nullptr);
        }
    } reset_memory;
    MissingPictureLandmarkModel mapping;
    auto& registry = service::ServiceRegistry::Instance();
    REQUIRE_FALSE(registry.Has<service::IModelPathMapping>());
    registry.Set<service::IModelPathMapping>(&mapping);
    struct ResetMapping {
        ~ResetMapping() {
            service::ServiceRegistry::Instance().Set<service::IModelPathMapping>(nullptr);
        }
    } reset_mapping;
    ActionNode action{};
    action.atomicCode = "1000005";
    action.configObject.params.resize(1);
    action.configObject.params[0].key   = "featureInput";
    action.configObject.params[0].value = "0";
    auto data                           = std::make_shared<AlgData>();
    data->chanDataDec.frame = std::make_shared<media::VideoFrame>(64, 64, media::PixelFormat::PIXEL_BGR8);
    REQUIRE(data->chanDataDec.frame->Active());
    data->chanDataDetect.detRet = std::make_shared<DataDetTrackClassify>();
    data->chanDataDetect.detRet->targets.resize(1);
    auto& target         = data->chanDataDetect.detRet->targets.front();
    auto expected        = util::ErrorEnum::ModelFileLack;
    bool needs_landmarks = true;
    SECTION("a missing landmark stage requests the enrollment landmark model") {}
    SECTION("a stale landmark flag cannot bypass missing points") {
        data->bHaveLandmark = true;
    }
    SECTION("related faces require their own landmarks") {
        target.landmark.landmark.resize(5);
        target.relatedEl.bActive = true;
    }
    SECTION("valid existing points are reused without loading another model") {
        target.landmark.landmark.resize(5);
        needs_landmarks = false;
        expected        = util::ErrorEnum::NotInit;
    }
    SECTION("empty detections need no landmark model") {
        data->chanDataDetect.detRet->targets.clear();
        needs_landmarks = false;
        expected        = util::ErrorEnum::Success;
    }
    SECTION("body embeddings do not load a face landmark model") {
        action.atomicCode                   = "1000006";
        action.configObject.params[0].value = "1";
        needs_landmarks                     = false;
        expected                            = util::ErrorEnum::NotInit;
    }
    PRecognizer worker("picture-face-landmark-regression", action);
    CHECK(worker.HandPic(data) == expected);
    if (needs_landmarks) {
        CHECK(mapping.requests == std::vector<std::string>{"1000016"});
    } else {
        CHECK(mapping.requests.empty());
    }
#endif
}
